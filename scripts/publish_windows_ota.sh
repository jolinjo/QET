#!/bin/bash
# Windows 版 OTA 發佈腳本(在 Windows 的 Git Bash 執行)
#
# 這台發佈機不需要建置工具鏈:Windows 免安裝版由 GitHub Actions
# 的 windows-ota.yml workflow 建置(Qt6/MSYS2),本腳本只負責
# 取得 artifact → 推進內網 QET-release repo(win-stable 分支),
# 打 win-vX.Y.Z tag 並只保留最近 KEEP 版。
#
# 用法:
#   scripts/publish_windows_ota.sh <artifact.zip | 免安裝目錄>
#   scripts/publish_windows_ota.sh --ci [RUN_ID]
#       --ci:用 gh CLI 下載 windows-ota.yml 最近一次成功建置的
#       artifact(或指定 RUN_ID)。需先 gh auth login。
#
# 需求:對 $REPO_URL 有 push 權限(git 認證先設定好)。
# 注意:artifact 必須由「目前 HEAD」的 commit 建置,版號才會與
#       CMakeLists.txt 一致(tag 取自 CMakeLists 的 VERSION)。

set -euo pipefail

REPO_URL="${QET_RELEASE_URL:-http://hc-server:3000/HC-Git/QET-release}"
BRANCH="win-stable"
TAG_PREFIX="win-v"
KEEP=3
GH_REPO="jolinjo/QET"
WORKFLOW="windows-ota.yml"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CACHE="$HOME/.qet-release-publish-win"
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/qet-ota-win.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT

VERSION=$(sed -n 's/^[[:space:]]*VERSION[[:space:]]*\([0-9][0-9.]*\)$/\1/p' "$ROOT/CMakeLists.txt" | head -1)
[ -n "$VERSION" ] || { echo "!! 讀不到 CMakeLists.txt 的 VERSION"; exit 1; }
TAG="${TAG_PREFIX}${VERSION}"
echo "== 發佈 win 版 v$VERSION → $REPO_URL ($BRANCH, tag $TAG)"

# Windows 內建 bsdtar 才解得開 zip(Git Bash 的 GNU tar 不行)
BSDTAR="$SYSTEMROOT/System32/tar.exe"
[ -f "$BSDTAR" ] || BSDTAR=tar

# 1. 取得免安裝版 ------------------------------------------------------------
SRC="${1:-}"
if [ "$SRC" = "--ci" ]; then
	command -v gh >/dev/null || { echo "!! --ci 需要 gh CLI(winget install GitHub.cli)"; exit 1; }
	RUN_ID="${2:-}"
	if [ -z "$RUN_ID" ]; then
		RUN_ID=$(gh run list -R "$GH_REPO" --workflow "$WORKFLOW" \
			--status success --limit 1 --json databaseId \
			--jq '.[0].databaseId')
		[ -n "$RUN_ID" ] || { echo "!! 找不到成功的 $WORKFLOW 建置"; exit 1; }
	fi
	echo "== 下載 CI artifact(run $RUN_ID)"
	gh run download "$RUN_ID" -R "$GH_REPO" \
		--pattern 'qelectrotech-win-portable-*' --dir "$STAGE/dl"
	# gh 會把每個 artifact 解到同名子目錄
	PORTABLE=$(dirname "$(find "$STAGE/dl" -type f -iname QElectroTech.exe | head -1)")/..
elif [ -d "$SRC" ]; then
	PORTABLE="$SRC"
elif [ -f "$SRC" ]; then
	echo "== 解壓 $SRC"
	mkdir -p "$STAGE/unzip"
	"$BSDTAR" -xf "$SRC" -C "$STAGE/unzip"
	PORTABLE=$(dirname "$(find "$STAGE/unzip" -type f -iname QElectroTech.exe | head -1)")/..
else
	echo "用法: $0 <artifact.zip | 免安裝目錄> | --ci [RUN_ID]"; exit 1
fi
PORTABLE=$(cd "$PORTABLE" && pwd)
[ -f "$PORTABLE/bin/QElectroTech.exe" ] || { echo "!! $PORTABLE 內找不到 bin/QElectroTech.exe"; exit 1; }
echo "== 免安裝版來源:$PORTABLE"

# 私有混合字型不在公開 repo/CI:發佈時由本機注入(app 執行時從
# fonts/ 載入)。本機沒有就略過並提醒。
HYBRID_FONT="$ROOT/fonts/YaHei.Consolas.1.11b.ttf"
if [ -f "$HYBRID_FONT" ]; then
	mkdir -p "$PORTABLE/fonts"
	cp -f "$HYBRID_FONT" "$PORTABLE/fonts/"
	echo "== 已注入混合字型 YaHei.Consolas.1.11b.ttf"
else
	echo "!! 提醒:$HYBRID_FONT 不存在,本版不含混合字型"
	echo "   (從 mac 機的 QET/fonts/ 複製過來即可,檔案已被 gitignore)"
fi

# 2. 推進 release repo -------------------------------------------------------
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
# 免安裝內容放 repo 根(client 端 tar --strip-components=1 直接對應),
# 保留更新紀錄與發佈狀態檔
find . -mindepth 1 -maxdepth 1 ! -name .git \
	! -name CHANGELOG-win.md ! -name .fork-sha-win -exec rm -rf {} +
cp -a "$PORTABLE"/. "$CACHE"/

# 3. 更新紀錄:彙整自上次發佈以來 fork 的所有 commit ---------------------------
FORK_SHA=$(git -C "$ROOT" rev-parse HEAD)
NOTES=""
if [ -f "$CACHE/.fork-sha-win" ]; then
	PREV_SHA=$(cat "$CACHE/.fork-sha-win")
	NOTES=$(git -C "$ROOT" log --no-merges --pretty='- %s' \
		"$PREV_SHA..HEAD" 2>/dev/null || true)
fi
CHANGELOG="$CACHE/CHANGELOG-win.md"
# 重跑冪等:沒有新 commit 且該版已記錄過,就不再增添區塊
if [ -z "$NOTES" ] && [ -f "$CHANGELOG" ] \
	&& grep -q "^## v$VERSION" "$CHANGELOG"; then
	:
else
	[ -n "$NOTES" ] || NOTES="- (無變更紀錄)"
	{
		echo "## v$VERSION($(date +%Y-%m-%d))"
		echo
		printf '%s\n' "$NOTES"
		echo
		[ -f "$CHANGELOG" ] && cat "$CHANGELOG"
	} > "$CHANGELOG.new"
	mv "$CHANGELOG.new" "$CHANGELOG"
fi
printf '%s\n' "$FORK_SHA" > "$CACHE/.fork-sha-win"

git add -A
if ! git diff --cached --quiet; then
	git commit -q -m "win 版 v$VERSION"
fi
git tag -f "$TAG"

# 只保留最近 KEEP 個 win tag
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
