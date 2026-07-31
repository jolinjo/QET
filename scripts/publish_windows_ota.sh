#!/bin/bash
# Windows 版 OTA 發佈腳本
#
# 把免安裝版當 Gitea release asset 發佈(靜態下載,不進 git 分支;
# 避免 Gitea 動態打包大 archive 觸發 0xc0000142)。win-stable 分支
# 只放輕量的 README + CHANGELOG(首頁與更新紀錄)。只保留最近 KEEP
# 個 win-v release。標準流程為本機建置(--build)。
#
# 用法:
#   scripts/publish_windows_ota.sh --build
#       --build:本機建置(MSYS2 UCRT64,scripts/build_windows_portable.sh
#       --release)後發佈。標準發佈流程。
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
if [ "$SRC" = "--build" ]; then
	bash "$ROOT/scripts/build_windows_portable.sh" --release
	PORTABLE="$ROOT/build-win/portable"
elif [ "$SRC" = "--ci" ]; then
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

# Gitea API 位址與認證(建 release / 上傳 asset 要寫入權限;用 git 憑證的
# basic auth)。免安裝包(含 400MB git)當 release asset 靜態下載,不進
# git 分支 -- 避免 Gitea 動態打包大 archive 觸發 0xc0000142。
HOST=$(echo "$REPO_URL" | sed -E 's#(^https?://[^/]+).*#\1#')
OWNER_REPO=$(echo "$REPO_URL" | sed -E 's#^https?://[^/]+/##')
API_BASE="$HOST/api/v1/repos/$OWNER_REPO"
CRED=$(printf 'protocol=%s\nhost=%s\n\n' "${HOST%%://*}" "${HOST#*://}" \
	| git credential fill 2>/dev/null)
GU=$(echo "$CRED" | sed -n 's/^username=//p')
GP=$(echo "$CRED" | sed -n 's/^password=//p')
[ -n "$GU" ] && [ -n "$GP" ] || { echo "!! 取不到 $HOST 的 git 認證"; exit 1; }
api() { curl -fsS -u "$GU:$GP" "$@"; }

# 2. 打包免安裝版為 zip(release asset;zip 根直接是 bin/ elements/...) --------
ZIPNAME="qelectrotech-${TAG}-win64.zip"
ZIPPATH="$STAGE/$ZIPNAME"
echo "== 打包 $ZIPNAME"
( cd "$PORTABLE" && "$BSDTAR" -a -cf "$ZIPPATH" . )
[ -f "$ZIPPATH" ] || { echo "!! 打包失敗"; exit 1; }
echo "   $ZIPNAME ($(du -h "$ZIPPATH" | cut -f1))"

# 3. win-stable 分支只放 README + CHANGELOG(輕量,首頁與更新紀錄用) -----------
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
# 只保留文字檔:清掉舊方案殘留在分支的 portable 內容(bin/ 等 400MB),
# 分支瘦身後 archive/web 不再吃重。
find . -mindepth 1 -maxdepth 1 ! -name .git \
	! -name CHANGELOG-win.md ! -name .fork-sha-win -exec rm -rf {} +

# 更新紀錄:彙整自上次發佈以來 fork 的所有 commit
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

LATEST_NOTES=$(awk '/^## /{n++} n==1' "$CHANGELOG")
DL_URL="$REPO_URL/releases/download/$TAG/$ZIPNAME"
cat > "$CACHE/README.md" <<EOF
# QElectroTech Windows 免安裝版

**目前版本:$TAG**(發佈日期:$(date +%Y-%m-%d))

## 首次安裝

1. 下載 [$ZIPNAME]($DL_URL)
2. 解壓到任意資料夾(例如 \`D:\\QET\`)
3. 執行 \`Lancer QET.bat\`(或 \`bin\\QElectroTech.exe\`)

需求:Windows 10 1803 以上(更新功能用系統內建 curl/tar),
**不需要**安裝 git 或任何其他軟體。

## 之後怎麼更新

程式內「**說明 → 檢查更新(內網)**」,選版本按「切換到選取版本」,
程式會自動關閉、更新、重新啟動。要退回舊版也是同一個地方。

## 最新版本更新內容

$LATEST_NOTES

完整更新紀錄見 [CHANGELOG-win.md](CHANGELOG-win.md)。
EOF

git add -A
if ! git diff --cached --quiet; then
	git commit -q -m "win 版 v$VERSION 說明(release asset)"
fi
git push origin "$BRANCH" --force-with-lease 2>/dev/null \
	|| git push origin "$BRANCH"

# 4. 建 Gitea release + 上傳 asset(取代舊的 archive 下載) --------------------
# 冪等:先刪同名 release 與 tag,再重建
OLD_ID=$(api "$API_BASE/releases/tags/$TAG" 2>/dev/null \
	| grep -o '"id":[0-9]*' | head -1 | cut -d: -f2 || true)
[ -n "$OLD_ID" ] && api -X DELETE "$API_BASE/releases/$OLD_ID" >/dev/null 2>&1 || true
api -X DELETE "$API_BASE/tags/$TAG" >/dev/null 2>&1 || true

# release body = 最新版更新內容(JSON escape:反斜線/引號/換行)
BODY_ESC=$(printf '%s' "$LATEST_NOTES" \
	| sed ':a;N;$!ba;s/\\/\\\\/g;s/"/\\"/g;s/\r//g;s/\n/\\n/g')
RID=$(api -X POST "$API_BASE/releases" -H "Content-Type: application/json" \
	-d "{\"tag_name\":\"$TAG\",\"target_commitish\":\"$BRANCH\",\"name\":\"$TAG\",\"body\":\"$BODY_ESC\"}" \
	| grep -o '"id":[0-9]*' | head -1 | cut -d: -f2)
[ -n "$RID" ] || { echo "!! 建 release 失敗"; exit 1; }
echo "== 上傳 asset(release id $RID)"
api -X POST "$API_BASE/releases/$RID/assets?name=$ZIPNAME" \
	-F "attachment=@$ZIPPATH;type=application/zip" >/dev/null \
	|| { echo "!! 上傳 asset 失敗"; exit 1; }

# 只保留最近 KEEP 個 win-v release(連同 tag)
ALL=$(api "$API_BASE/releases?limit=50" \
	| grep -o '"tag_name":"'"$TAG_PREFIX"'[0-9.]*"' | cut -d'"' -f4 | sort -V)
COUNT=$(printf '%s\n' "$ALL" | grep -c . || true)
if [ "$COUNT" -gt "$KEEP" ]; then
	printf '%s\n' "$ALL" | head -n $((COUNT - KEEP)) | while read -r t; do
		[ -n "$t" ] || continue
		rid=$(api "$API_BASE/releases/tags/$t" \
			| grep -o '"id":[0-9]*' | head -1 | cut -d: -f2 || true)
		[ -n "$rid" ] && api -X DELETE "$API_BASE/releases/$rid" >/dev/null 2>&1 || true
		api -X DELETE "$API_BASE/tags/$t" >/dev/null 2>&1 || true
		echo "   清理舊版 $t"
	done
fi

echo "== 發佈完成:$TAG(release asset $ZIPNAME)"
echo "   保留 release:$(api "$API_BASE/releases?limit=50" | grep -o '"tag_name":"'"$TAG_PREFIX"'[0-9.]*"' | cut -d'"' -f4 | sort -V | tr '\n' ' ')"
