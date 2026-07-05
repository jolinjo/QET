# Windows (MSVC) 編譯踩坑指南

> 本文件記錄在 **Windows + Visual Studio 2022 (MSVC) + Qt6** 下從原始碼建置 QElectroTech 實際遇到的問題與解法。
> QET 官方 Windows 建置流程是 **MinGW**（見 [`md/fr/fr_window_build_summary.md`](../md/fr/fr_window_build_summary.md)）。若你用 MSVC，以下坑一定會踩到。

## TL;DR — 給 AI / 自動化的重點

1. **QET 官方 Windows 只支援 MinGW。** CMake 中大量 `if(NOT MINGW)` 其實是「UNIX / Linux 區塊」，MSVC 會誤入 → 需用 `-D` 補齊數個路徑變數才能 configure 過。**C++ 原始碼本身在 MSVC 完全編得過**（693 步全綠）。
2. clone 時 `doc/QElectroTech.qch`（Git LFS）可能在 server 上 **404** → 用 `GIT_LFS_SKIP_SMUDGE=1` 略過。
3. KF6 / pugixml / SingleApplication 由 CMake **FetchContent 從外部 git 自動抓編**；`-DBUILD_KF6=ON` 前提是先裝好 **ECM ≥ 6.22.0**。
4. `find_package(SQLite3 REQUIRED)` 在 Windows 無系統 SQLite → 需自備 `sqlite3.h` + lib 並用 `-DSQLite3_INCLUDE_DIR` / `-DSQLite3_LIBRARY` 指定。
5. **免安裝打包**：`windeployqt` 不帶 `pugixml.dll` 與 VC++ runtime → 缺了會「找不到 DLL」開不了（坑 5）。
6. **全零殘留**：QSettings 預設寫**登錄檔**；本 fork 在 `main.cpp` 加了 `--config-dir` 時改用本機 INI 的 patch（坑 6）。`overrideDataDir/ConfigDir` 要求目標夾**先存在**才生效。
7. **Windows 腳本編碼**：`.bat` 要「無 BOM 且註解純 ASCII」（否則 cmd 亂碼＋安全封鎖框）；`.ps1`（PS 5.1）剛好相反，要「有 BOM 或純 ASCII」（否則中文字面值亂碼）。方向相反（坑 7）。

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
若跳出「找不到 XXX.dll」的系統錯誤框，它自己也是一個視窗，`MainWindowTitle` 會顯示成
exe 路徑而非 `QElectroTech`——別把錯誤框誤判成 app 成功啟動（見坑 5）。

---

## 免安裝（portable）打包

`build\qelectrotech.exe` + 同夾 DLL 即可執行、不需安裝。元件庫/標題欄已內嵌在 exe（qrc），
但要做成可散布的免安裝夾，還有幾個坑。

## 坑 5：windeployqt 不處理第三方 DLL（pugixml.dll / VC++ runtime）

`windeployqt` 只複製 **Qt 自己的 DLL**。QET 額外相依：

- **`pugixml.dll`** —— 以 shared library 編出（`-DBUILD_PUGIXML=ON`）。從 `build\` 跑沒事是因為它就在那；
  複製 exe 到別的夾卻漏了它 → 啟動時「**找不到 pugixml.dll，無法繼續執行代碼**」。
  （SingleApplication 與 KF6WidgetsAddons 是靜態連結。**但注意**：從**全新 build/**
  （砍掉重建）時 **KF6CoreAddons 可能被編成 shared**，產生 `build\bin\KF6CoreAddons.dll`
  ——此時直接跑 `build\qelectrotech.exe` 會「找不到 KF6CoreAddons.dll」。解法：把
  `build\bin\*.dll` 一併複製到 exe 旁（make_portable 已自動處理）。增量重建時多半是靜態、不會有此 DLL。）
- **VC++ runtime**（`msvcp140*.dll` / `vcruntime140*.dll` / `concrt140.dll`）—— MSVC `/MD` 動態連結 CRT。
  本機有裝 VS 所以能跑，但**乾淨機器會缺**。windeployqt 只附 `vc_redist.x64.exe`（安裝器）；
  要真正免安裝就把這幾個 DLL 直接複製進夾（來源：
  `…\VC\Redist\MSVC\<ver>\x64\Microsoft.VC143.CRT\`），並刪掉 `vc_redist.x64.exe`。

## 坑 6：全零殘留 —— QSettings 在 Windows 預設寫「登錄檔」

QET 全程用 `QSettings settings;`（預設建構子）。**Windows 上預設後端是登錄檔**
`HKEY_CURRENT_USER\Software\QElectroTech`，不是檔案。所以即使帶 `--config-dir`，主偏好設定
（視窗版面、dock 狀態…）仍寫登錄檔 → **不是零殘留**。

> ⚠️ `machine_info.cpp` 那行 `App-Config: see Registry "HKCU/..."` 是**寫死的字串**，
> 不反映實際格式，不能拿來判斷。要確認就直接看 **有沒有生成 `.ini`** 與 **登錄檔鍵是否被建立**。

**本 fork 的解法**（`sources/main.cpp`，最小改動、只在有 `--config-dir` 時生效，安裝版行為不變）：
在 `setApplicationName` 之後、第一次讀 QSettings 之前，把 QSettings 切成該資料夾的本機 INI：

```cpp
for (int i = 1; i < argc; ++i) {
    const QString option = QString::fromLocal8Bit(argv[i]);
    const QString cd_arg = QStringLiteral("--config-dir=");
    if (option.startsWith(cd_arg)) {
        const QString dir = option.mid(cd_arg.length());
        if (!dir.isEmpty()) {
            QSettings::setDefaultFormat(QSettings::IniFormat);
            QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir);
        }
        break;
    }
}
```

設定改寫到 `<config-dir>\QElectroTech\QElectroTech.ini`，登錄檔完全乾淨。

**另一個關聯坑**：`QETApp::overrideDataDir()` / `overrideConfigDir()` 內有
`if (QFileInfo(new_dd).isDir())` 判斷 —— **目標資料夾必須先存在**，否則 override 被靜默略過
（症狀：路徑沒改、資料還是跑去 AppData）。所以啟動器必須先 `mkdir config data` 再啟動。

## 坑 7：.bat 用中文（UTF-8）註解 → cmd 亂碼 + Windows 安全對話框

Windows `cmd` 用 OEM codepage（繁中系統 = Big5/950）解讀 .bat。若 .bat 存成 **UTF-8 含中文註解**，
中文 `REM` 會被解成亂碼，其中片段被當指令執行（`'-release' 不是內部或外部命令`…），
甚至觸發 Windows 附件管理員跳「**無法打開這些文件…你的 Internet 安全設置阻止…**」封鎖框
（常指向 `…\Git\mingw64\bin\nul`，因 `>nul` 在被污染的環境下被誤解析成檔案）。

**解法：所有 .bat 註解只用 ASCII（英文）**，且存檔**不要有 UTF-8 BOM**（BOM 會讓第一行出錯）。
使用者會雙擊的啟動器尤其要注意。

**反向坑 —— PowerShell 5.1 剛好相反**：Windows PowerShell 5.1（`powershell.exe`）讀 `.ps1` 時，
**沒有 BOM 的 UTF-8 會被當成 ANSI/Big5**，腳本裡的中文字面值會變亂碼（例：`啟動` → `鍟熷嫊`），
導致產生錯誤檔名、字串比對失敗。兩個解法擇一：
（a）`.ps1` 存成 **UTF-8 with BOM**（PS 5.1 認 BOM）；或
（b）**腳本保持純 ASCII**，需要的中文用字元碼組出（如 `[char]0x555F + [char]0x52D5`）。
本專案的 `make_portable.ps1` 走 (b)，故打包腳本完全不含中文字面值。
> 一句話：**.bat 要「無 BOM」，.ps1（PS 5.1）要「有 BOM」或純 ASCII** —— 方向相反，別搞混。

## 坑 8：官方元件庫不是內嵌，是 exe 旁的 `elements\` 資料夾

QET 的共用元件庫（8000+ 個元件）與標題欄範本是**磁碟上的 `elements\` / `titleblocks\` 資料夾**，
**不是**編進 qrc。只複製 exe + DLL 的免安裝夾，元件庫面板會是空的（只剩空的「公司/用戶元件庫」）。

路徑邏輯（`QETApp::commonElementsDir()`）：`QET_COMMON_COLLECTION_PATH="elements/"`，而
`QET_COMMON_COLLECTION_PATH_RELATIVE_TO_BINARY_PATH` **只在 `if(APPLE)` 定義**，Windows 沒有 →
QET 用的是**相對 CWD** 的 `./elements/`，不是 exe 目錄。

**解法**：把 repo 的 `elements\`（約 103 MB，本身是 git submodule，需先 `git submodule update --init`）
與 `titleblocks\` 複製進夾，並在啟動器用絕對路徑明確指定（最穩、不依賴 CWD）：
`--common-elements-dir="%HERE%elements" --common-tbt-dir="%HERE%titleblocks"`。
確認方式：啟動 log 應出現 `Common Elements count: 8643 Elements`（非 0）。

## 免安裝夾組成（實測可攜、零殘留、含完整元件庫）

```text
QElectroTech-portable\
  qelectrotech.exe                 (--config-dir 時走本機 INI 的 patch 版)
  Qt6*.dll  platforms\ styles\ sqldrivers\ tls\ imageformats\ ...   (windeployqt)
  pugixml.dll                       (坑 5)
  msvcp140*.dll vcruntime140*.dll concrt140.dll                     (坑 5)
  elements\  titleblocks\           (坑 8：官方元件庫/標題欄，~105 MB)
  啟動 QElectroTech.bat             (先 mkdir config/data，帶 config/data/elements/tbt dir 啟動)
  QElectroTech.lnk                  (帶 QET 圖示的捷徑，最小化執行上面的 .bat；建議雙擊這個)
```

> 使用者應雙擊 `QElectroTech.lnk`（或那個 .bat），**不要**直接點 `qelectrotech.exe`——
> 直接點 exe 少了那些 `--xxx-dir` 參數，會退回原生行為（寫登錄檔/AppData、元件庫載不到）。

啟動後狀態全落在夾內 `config\QElectroTech\QElectroTech.ini`（設定）與 `data\`（使用者集合/cache/log），
`HKCU\Software\QElectroTech` 不被建立。整夾約 145 MB。
