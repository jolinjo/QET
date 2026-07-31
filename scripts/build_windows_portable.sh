#!/bin/bash
# 本機建置 Windows 免安裝版(MSYS2 UCRT64 + Qt6,環境裝在 C:\msys64)
#
# 產出:build-win/portable/(bin/QElectroTech.exe + DLLs + elements/ + lang/ ...)
# 與 CI windows-ota.yml 的佈局一致;發佈用 publish_windows_ota.sh --build。
#
# 用法: scripts/build_windows_portable.sh [--release]
#   --release: QET_RELEASE_BUILD=ON(版號無 -dev 字尾),OTA 正式版一律用。

# 不在 UCRT64 環境時(例如從 Git Bash 或 cmd 呼叫),自動重新進入
# (login shell 會回到家目錄,路徑必須用絕對路徑)
if [ "${MSYSTEM:-}" != "UCRT64" ]; then
	SELF="$(cd "$(dirname "$0")" && pwd)/$(basename "$0")"
	exec env MSYSTEM=UCRT64 CHERE_INVOKING=1 \
		/c/msys64/usr/bin/bash -l "$SELF" "$@"
fi

set -euo pipefail

# ccache 需要 USERPROFILE/LOCALAPPDATA;經 login shell 重新進入或沙盒
# 環境可能遺失,直接固定快取目錄最穩
export USERPROFILE="${USERPROFILE:-$(cygpath -w "$HOME")}"
export CCACHE_DIR="${CCACHE_DIR:-$HOME/.ccache}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build-win"
FILES="$BUILD/portable"
BIN="$FILES/bin"

RELEASE_FLAG=OFF
[ "${1:-}" = "--release" ] && RELEASE_FLAG=ON
echo "== 本機建置 Windows 免安裝版(QET_RELEASE_BUILD=$RELEASE_FLAG)"

# 1. 建置 ------------------------------------------------------------------
cmake -B "$BUILD" -S "$ROOT" -G Ninja \
	-DCMAKE_BUILD_TYPE=Release \
	-DQET_RELEASE_BUILD=$RELEASE_FLAG \
	-DBUILD_TESTING=OFF \
	-DCMAKE_C_COMPILER_LAUNCHER=ccache \
	-DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
	-DSQLite3_INCLUDE_DIR=/ucrt64/include \
	-DSQLite3_LIBRARY=/ucrt64/lib/libsqlite3.dll.a
ninja -C "$BUILD"

EXE=$(find "$BUILD" -maxdepth 2 -iname "qelectrotech.exe" ! -path "*/portable/*" | head -1)
[ -n "$EXE" ] || { echo "!! 建置後找不到 qelectrotech.exe"; exit 1; }

# 2. 組免安裝佈局 ------------------------------------------------------------
rm -rf "$FILES"
mkdir -p "$BIN"
cp "$EXE" "$BIN/QElectroTech.exe"

WDQ=""
for cand in /ucrt64/bin/windeployqt6.exe /ucrt64/bin/windeployqt-qt6.exe /ucrt64/bin/windeployqt.exe; do
	[ -f "$cand" ] && { WDQ="$cand"; break; }
done
[ -n "$WDQ" ] || { echo "!! 找不到 windeployqt"; exit 1; }
(cd "$BIN" && "$WDQ" --release --no-translations --no-compiler-runtime ./QElectroTech.exe) || true

# 遞移相依 DLL 掃描(3 輪,涵蓋外掛的間接相依)
set +e
for PASS in 1 2 3; do
	for bin_file in "$BIN"/*.dll "$BIN"/*.exe "$BIN"/*/*.dll; do
		[ -f "$bin_file" ] || continue
		while IFS= read -r line; do
			dll_path=$(echo "$line" | awk '{print $3}')
			[ -f "$dll_path" ] || continue
			dll_name=$(basename "$dll_path")
			[ -f "$BIN/$dll_name" ] || cp "$dll_path" "$BIN/$dll_name"
		done < <(ldd "$bin_file" 2>/dev/null | grep -i '/ucrt64/bin/')
	done
done
set -e
cp -f /ucrt64/bin/libgcc_s_seh-1.dll /ucrt64/bin/libstdc++-6.dll \
	/ucrt64/bin/libwinpthread-1.dll "$BIN/"

DLL_COUNT=$(find "$BIN" -name "*.dll" | wc -l)
[ "$DLL_COUNT" -gt 5 ] || { echo "!! DLL 數量異常($DLL_COUNT)"; exit 1; }

# 3. 資料目錄與啟動捷徑 -------------------------------------------------------
cp -r "$ROOT/elements"    "$FILES/elements"
cp -r "$ROOT/titleblocks" "$FILES/titleblocks"
cp -r "$ROOT/examples"    "$FILES/examples" 2>/dev/null || true
cp -r "$ROOT/lang"        "$FILES/lang"
find "$BUILD" -name "*.qm" -not -path "*/portable/*" -exec cp {} "$FILES/lang/" \; 2>/dev/null || true
rm -rf "$FILES/elements/.git" "$FILES/lang/"*.ts 2>/dev/null || true
# 本機 fonts/(含私有混合字型,若已放入)一併帶進免安裝包
if [ -d "$ROOT/fonts" ]; then
	mkdir -p "$FILES/fonts"
	cp "$ROOT/fonts/"*.ttf "$ROOT/fonts/"*.ttc "$ROOT/fonts/"*.otf \
		"$FILES/fonts/" 2>/dev/null || true
fi
cp "$ROOT/build-aux/windows/Lancer QET.bat" "$FILES/Lancer QET.bat"
for f in LICENSE ChangeLog CREDIT README ELEMENTS.LICENSE; do
	cp "$ROOT/$f" "$FILES/$f" 2>/dev/null || true
done

# 4. 打包 git + git-lfs(PDM 出庫/入庫用;同事機器零依賴不裝 git) --------------
# MinGit(可攜版 git)解到 git/,git-lfs.exe 塞進 git/mingw64/bin,
# 執行時由 PdmGitWorker 把 git/cmd 與 git/mingw64/bin 注入 PATH。
MINGIT_VER="2.47.1"
GITLFS_VER="3.7.1"
MINGIT_URL="https://github.com/git-for-windows/git/releases/download/v${MINGIT_VER}.windows.1/MinGit-${MINGIT_VER}-64-bit.zip"
GITLFS_URL="https://github.com/git-lfs/git-lfs/releases/download/v${GITLFS_VER}/git-lfs-windows-amd64-v${GITLFS_VER}.zip"
GITCACHE="$BUILD/git-portable-cache"
mkdir -p "$GITCACHE"
MINGIT_ZIP="$GITCACHE/mingit-${MINGIT_VER}.zip"
GITLFS_ZIP="$GITCACHE/git-lfs-${GITLFS_VER}.zip"
[ -f "$MINGIT_ZIP" ] || curl -fsSL "$MINGIT_URL" -o "$MINGIT_ZIP"
[ -f "$GITLFS_ZIP" ] || curl -fsSL "$GITLFS_URL" -o "$GITLFS_ZIP"

TAR="$SYSTEMROOT/System32/tar.exe"
GITDIR="$FILES/git"
rm -rf "$GITDIR"; mkdir -p "$GITDIR"
"$TAR" -xf "$MINGIT_ZIP" -C "$GITDIR"   # MinGit zip 解開即 cmd/ mingw64/ ...
[ -f "$GITDIR/cmd/git.exe" ] || { echo "!! MinGit 解壓後找不到 cmd/git.exe"; exit 1; }

LFSTMP="$GITCACHE/lfs-extract"; rm -rf "$LFSTMP"; mkdir -p "$LFSTMP"
"$TAR" -xf "$GITLFS_ZIP" -C "$LFSTMP"
LFSEXE=$(find "$LFSTMP" -iname git-lfs.exe | head -1)
[ -n "$LFSEXE" ] || { echo "!! git-lfs zip 內找不到 git-lfs.exe"; exit 1; }
cp "$LFSEXE" "$GITDIR/mingw64/bin/git-lfs.exe"

# 功能驗證(勝過 hash 校驗:直接確認打包的執行檔真的能跑)
"$GITDIR/cmd/git.exe" --version >/dev/null \
	|| { echo "!! 打包的 git 無法執行"; exit 1; }
PATH="$GITDIR/mingw64/bin:$PATH" "$GITDIR/cmd/git.exe" lfs version >/dev/null \
	|| { echo "!! 打包的 git-lfs 無法執行"; exit 1; }
echo "== 已打包 git $MINGIT_VER + git-lfs $GITLFS_VER"

echo "== 完成:$FILES"
"$BIN/QElectroTech.exe" --version || true
