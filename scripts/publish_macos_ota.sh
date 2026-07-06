#!/bin/bash
# mac 版 OTA 發佈腳本
#
# 流程:建置 → macdeployqt 自包含打包 → 修正殘留參照 → ad-hoc 簽章 →
#       自包含稽核 → 推進內網 QET-release repo(mac-stable 分支),
#       打 mac-vX.Y.Z tag 並只保留最近 KEEP 版。
#
# 用法:  scripts/publish_macos_ota.sh [--no-build|--release]
#        --release:以 QET_RELEASE_BUILD=ON 重建(版號無 -dev),發佈後
#        還原為 OFF。OTA 正式版一律用 --release。
# 需求:  對 $REPO_URL 有 push 權限(git 認證先設定好)。

set -euo pipefail

REPO_URL="${QET_RELEASE_URL:-http://hc-server:3000/HC-Git/QET-release}"
BRANCH="mac-stable"
TAG_PREFIX="mac-v"
KEEP=3

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CACHE="$HOME/.qet-release-publish"
STAGE="$(mktemp -d /tmp/qet-ota-stage.XXXXXX)"
trap 'rm -rf "$STAGE"' EXIT

VERSION=$(sed -n 's/^[[:space:]]*VERSION[[:space:]]*\(0\.[0-9.]*\)$/\1/p' "$ROOT/CMakeLists.txt" | head -1)
[ -n "$VERSION" ] || { echo "!! 讀不到 CMakeLists.txt 的 VERSION"; exit 1; }
TAG="${TAG_PREFIX}${VERSION}"
echo "== 發佈 mac 版 v$VERSION → $REPO_URL ($BRANCH, tag $TAG)"

# 1. 建置 ------------------------------------------------------------------
if [ "${1:-}" = "--release" ]; then
	cmake -B "$ROOT/build" -S "$ROOT" -DQET_RELEASE_BUILD=ON >/dev/null
	cmake --build "$ROOT/build"
	trap 'cmake -B "$ROOT/build" -S "$ROOT" -DQET_RELEASE_BUILD=OFF >/dev/null; rm -rf "$STAGE"' EXIT
elif [ "${1:-}" != "--no-build" ]; then
	cmake --build "$ROOT/build"
fi
[ -d "$ROOT/build/qelectrotech.app" ] || { echo "!! 找不到 build/qelectrotech.app"; exit 1; }

# 2. 自包含打包 --------------------------------------------------------------
cp -R "$ROOT/build/qelectrotech.app" "$STAGE/"
APP="$STAGE/qelectrotech.app"
/opt/homebrew/opt/qt/bin/macdeployqt "$APP" 2>&1 \
	| grep -v "QtVirtualKeyboard" | grep "^ERROR" || true

# 3. 修正 macdeployqt 沒收乾淨的參照 -----------------------------------------
BIN="$APP/Contents/MacOS/qelectrotech"
# 主程式殘留的 homebrew rpath
RPATHS=$(otool -l "$BIN" | awk '/LC_RPATH/{f=1} f&&/path /{print $2; f=0}' \
	| grep "^/opt/homebrew" || true)
for rp in $RPATHS; do
	install_name_tool -delete_rpath "$rp" "$BIN"
done
# bundle 內所有 Mach-O:ID 或相依仍指向 /opt/homebrew 的,改指 bundle 內
# (含 .framework 內部二進位彼此的參照,macdeployqt 常漏)
find "$APP/Contents" -type f | while read -r f; do
	file -b "$f" | grep -q "Mach-O" || continue
	ID=$(otool -D "$f" 2>/dev/null | tail -1 || true)
	case "$ID" in /opt/homebrew/*)
		install_name_tool -id \
			"@executable_path/../Frameworks/${ID##*/lib/}" "$f" ;;
	esac
	DEPS=$(otool -L "$f" | tail -n +2 | awk '{print $1}' \
		| grep "^/opt/homebrew" || true)
	for dep in $DEPS; do
		suffix="${dep##*/lib/}"
		if [ -e "$APP/Contents/Frameworks/$suffix" ]; then
			install_name_tool -change "$dep" \
				"@executable_path/../Frameworks/$suffix" "$f"
		fi
	done
done

# 4. ad-hoc 簽章 -------------------------------------------------------------
codesign --force --deep --sign - "$APP" 2>/dev/null

# 5. 自包含稽核:所有 Mach-O 不得再參照 /opt/homebrew -------------------------
LEAK=$(find "$APP" -type f | while read -r f; do
	file -b "$f" | grep -q Mach-O || continue
	{ otool -L "$f" 2>/dev/null | tail -n +2 | grep "/opt/homebrew" || true; } \
		| sed "s|^|$(basename "$f"):|"
done)
if [ -n "$LEAK" ]; then
	echo "!! 仍有 homebrew 參照,中止:"; echo "$LEAK"; exit 1
fi
echo "== 自包含稽核通過"

# 6. 推進 release repo -------------------------------------------------------
if [ ! -d "$CACHE/.git" ]; then
	git clone "$REPO_URL" "$CACHE"
fi
cd "$CACHE"
git fetch origin --tags --force --prune --prune-tags 2>/dev/null || true
if git rev-parse --verify -q "origin/$BRANCH" >/dev/null; then
	git checkout -f "$BRANCH"
	git reset --hard "origin/$BRANCH"
elif git rev-parse --verify -q "$BRANCH" >/dev/null; then
	git checkout -f "$BRANCH"
else
	git checkout --orphan "$BRANCH"
	git rm -rfq --cached . 2>/dev/null || true
	find . -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
fi
rsync -a --delete "$APP/" "$CACHE/qelectrotech.app/"

# 7. 更新紀錄:彙整自上次發佈以來 fork 的所有 commit ---------------------------
FORK_SHA=$(git -C "$ROOT" rev-parse HEAD)
NOTES=""
if [ -f "$CACHE/.fork-sha" ]; then
	PREV_SHA=$(cat "$CACHE/.fork-sha")
	NOTES=$(git -C "$ROOT" log --no-merges --pretty='- %s' \
		"$PREV_SHA..HEAD" 2>/dev/null || true)
fi
[ -n "$NOTES" ] || NOTES="- (無變更紀錄)"
CHANGELOG="$CACHE/CHANGELOG-mac.md"
{
	echo "## v$VERSION($(date +%Y-%m-%d))"
	echo
	printf '%s\n' "$NOTES"
	echo
	[ -f "$CHANGELOG" ] && cat "$CHANGELOG"
} > "$CHANGELOG.new"
mv "$CHANGELOG.new" "$CHANGELOG"
printf '%s\n' "$FORK_SHA" > "$CACHE/.fork-sha"

git add -A
if ! git diff --cached --quiet; then
	git commit -q -m "mac 版 v$VERSION"
fi
git tag -f "$TAG"

# 只保留最近 KEEP 個 mac tag(macOS head 不支援負數行數,自己算)
ALL_TAGS=$(git tag -l "${TAG_PREFIX}*" | sort -V)
TAG_COUNT=$(printf '%s\n' "$ALL_TAGS" | grep -c . || true)
if [ "$TAG_COUNT" -gt "$KEEP" ]; then
	OLD_TAGS=$(printf '%s\n' "$ALL_TAGS" | head -n $((TAG_COUNT - KEEP)))
	for t in $OLD_TAGS; do
		git tag -d "$t"
		git push origin ":refs/tags/$t" 2>/dev/null || true
	done
fi

git push origin "$BRANCH" --force-with-lease 2>/dev/null \
	|| git push origin "$BRANCH"
git push origin "$TAG" --force
echo "== 發佈完成:$TAG(保留 tags:$(git tag -l "${TAG_PREFIX}*" | sort -V | tr '\n' ' '))"
