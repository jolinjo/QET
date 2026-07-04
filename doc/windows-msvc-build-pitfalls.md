# Windows (MSVC) 編譯踩坑指南

> 本文件記錄在 **Windows + Visual Studio 2022 (MSVC) + Qt6** 下從原始碼建置 QElectroTech 實際遇到的問題與解法。
> QET 官方 Windows 建置流程是 **MinGW**（見 [`md/fr/fr_window_build_summary.md`](../md/fr/fr_window_build_summary.md)）。若你用 MSVC，以下坑一定會踩到。

## TL;DR — 給 AI / 自動化的重點

1. **QET 官方 Windows 只支援 MinGW。** CMake 中大量 `if(NOT MINGW)` 其實是「UNIX / Linux 區塊」，MSVC 會誤入 → 需用 `-D` 補齊數個路徑變數才能 configure 過。**C++ 原始碼本身在 MSVC 完全編得過**（693 步全綠）。
2. clone 時 `doc/QElectroTech.qch`（Git LFS）可能在 server 上 **404** → 用 `GIT_LFS_SKIP_SMUDGE=1` 略過。
3. KF6 / pugixml / SingleApplication 由 CMake **FetchContent 從外部 git 自動抓編**；`-DBUILD_KF6=ON` 前提是先裝好 **ECM ≥ 6.22.0**。
4. `find_package(SQLite3 REQUIRED)` 在 Windows 無系統 SQLite → 需自備 `sqlite3.h` + lib 並用 `-DSQLite3_INCLUDE_DIR` / `-DSQLite3_LIBRARY` 指定。

---

## 相依需求

| 元件 | 說明 |
|---|---|
| VS2022 BuildTools MSVC | 提供 `cl` / `link`，並內建 CMake + Ninja |
| Qt 6.8.x `msvc2022_64` | 建議用 `aqtinstall`：`python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 --outputdir C:\Qt`（含 Widgets/PrintSupport/Xml/Svg/Sql/Network/Concurrent/qttools） |
| ECM ≥ 6.22.0 | KDE Extra CMake Modules，純 CMake 腳本，configure+install 到某 prefix 即可 |
| KF6 CoreAddons / WidgetsAddons | `-DBUILD_KF6=ON` 由 FetchContent 抓 `v6.22.0` 自動編 |
| pugixml | `-DBUILD_PUGIXML=ON` 由 FetchContent 抓 |
| SingleApplication | FetchContent 自動抓 |
| SQLite3 | 官方 amalgamation 自編（見下） |

---

## 坑 1：Git LFS 物件 404

clone 時 checkout 中斷：

```
Error downloading object: doc/QElectroTech.qch ... [404] Object does not exist on the server
smudge filter lfs failed
```

該檔是預編說明文件，不影響原始碼。略過即可：

```bash
GIT_LFS_SKIP_SMUDGE=1 git clone https://github.com/<user>/QET.git .
# 已 clone 但 checkout 失敗時：
GIT_LFS_SKIP_SMUDGE=1 git checkout HEAD -- .
```

## 坑 2：SQLite3 找不到

CMake `find_package(SQLite3 REQUIRED)` 在 Windows 沒有系統 SQLite。用官方 amalgamation 自編一個 static lib：

```bat
:: 下載 https://www.sqlite.org/<year>/sqlite-amalgamation-<ver>.zip 解壓後
cl /nologo /c /O2 /MD /DSQLITE_ENABLE_FTS5 /DSQLITE_ENABLE_JSON1 sqlite3.c
lib /nologo /OUT:sqlite3.lib sqlite3.obj
```

configure 時指定：

```
-DSQLite3_INCLUDE_DIR="<amalgamation 目錄>"
-DSQLite3_LIBRARY="<amalgamation 目錄>\sqlite3.lib"
```

## 坑 3：`if(NOT MINGW)` 區塊的空變數（MSVC 專屬）

`cmake/paths_compilation_installation.cmake` 的 `WIN32` 區塊**只設了部分路徑變數**，但 `cmake/define_definitions.cmake` 與 `CMakeLists.txt` 的 `if(NOT MINGW)` 區塊又去用其它變數。MSVC 因為「不是 MINGW」而進入這些 UNIX 導向區塊，導致：

```
# define_definitions.cmake:43
if given arguments: "STRGREATER ""  → Unknown arguments specified   (QET_EXAMPLES_PATH 為空)

# CMakeLists.txt:129~141
install DIRECTORY given no DESTINATION!   (QET_ICONS_PATH / QET_APPDATA_PATH 為空)
```

**解法（不改原始碼，用 `-D` 補值即可通過 configure；因為我們從 build 目錄執行、不做 `cmake --install`，值本身不重要）：**

```
-DQET_EXAMPLES_PATH=examples/
-DQET_ICONS_PATH=share/icons/
-DQET_APPDATA_PATH=share/metainfo/
```

> 若之後升級或想乾淨支援 MSVC，正解是在 `paths_compilation_installation.cmake` 的 `WIN32` 區塊補齊這些變數，或把 `if(NOT MINGW)` 改成 `if(UNIX)` / `if(NOT WIN32)`。

## 坑 4：FetchContent 需要網路 + 授權

configure 會從 `invent.kde.org`（ECM/KF6）與 GitHub（pugixml/SingleApplication）clone 並在本機編譯外部原始碼。在受限 / sandbox 環境需先允許此類動作。`-DBUILD_KF6=ON` 之前，ECM 必須已可被 `find_package(ECM 6.22.0 REQUIRED NO_MODULE)` 找到。

---

## 完整建置流程（參考）

```bat
:: 1) 環境（載入 MSVC + Qt + 內建 CMake/Ninja；CMAKE_PREFIX_PATH 指到 Qt 與相依 prefix）
call winenv.bat

:: 2) configure
cmake -G Ninja -S . -B build ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DBUILD_KF6=ON -DBUILD_PUGIXML=ON ^
  -DSQLite3_INCLUDE_DIR="...\sqlite-amalgamation-xxxx" ^
  -DSQLite3_LIBRARY="...\sqlite-amalgamation-xxxx\sqlite3.lib" ^
  -DQET_EXAMPLES_PATH=examples/ -DQET_ICONS_PATH=share/icons/ -DQET_APPDATA_PATH=share/metainfo/

:: 3) build
cmake --build build --parallel

:: 4) 部署 Qt DLL 後即可執行 build\qelectrotech.exe
windeployqt --release build\qelectrotech.exe
```

## 驗證

啟動後**須確認真的開出視窗**（主視窗標題 `QElectroTech`），不要只看行程存在。
