# 下載官方 git-lfs（macOS arm64）以打包進 .app，讓使用者免另外安裝。
# 圖檔管理(PDM)的出庫/入庫鎖定靠 `git lfs lock`，而 Finder 啟動的 .app
# PATH 精簡、找不到系統/brew 的 git-lfs，故隨 app 附帶並由程式注入路徑。
set(GITLFS_VERSION "3.7.1")
set(GITLFS_SHA256 "76260fb34f4ee622ff0a66b857e5954aa49c7e343a92e57a1ec4a760618c94b2")
set(GITLFS_DL_DIR "${CMAKE_BINARY_DIR}/git-lfs-download")
set(GITLFS_BINARY "${GITLFS_DL_DIR}/git-lfs-${GITLFS_VERSION}/git-lfs"
    CACHE FILEPATH "打包用 git-lfs 執行檔路徑")

if(NOT EXISTS "${GITLFS_BINARY}")
  set(_gitlfs_zip "${GITLFS_DL_DIR}/git-lfs.zip")
  message(STATUS "Downloading git-lfs ${GITLFS_VERSION} for app bundling…")
  file(DOWNLOAD
    "https://github.com/git-lfs/git-lfs/releases/download/v${GITLFS_VERSION}/git-lfs-darwin-arm64-v${GITLFS_VERSION}.zip"
    "${_gitlfs_zip}"
    EXPECTED_HASH SHA256=${GITLFS_SHA256}
    SHOW_PROGRESS)
  file(ARCHIVE_EXTRACT INPUT "${_gitlfs_zip}" DESTINATION "${GITLFS_DL_DIR}")
endif()

if(NOT EXISTS "${GITLFS_BINARY}")
  message(FATAL_ERROR "git-lfs 下載/解壓失敗，找不到 ${GITLFS_BINARY}")
endif()
