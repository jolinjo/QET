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

## 建置加速（給 AI：先讀完，別再重新摸索一次）

> 這段記錄了「編譯到底多久」「哪些是誤判」「怎麼一路改善到現在」。2026-07-05 大改：導入 **sccache**、**重新啟用 PCH**。

### 現行方案（結論先講）

1. **sccache 編譯器快取**（最大功臣）：`C:\tools\sccache\sccache.exe`（v0.8.2 可攜）。
   - `winenv.bat`：把 `C:\tools\sccache` 加進 PATH，設 `SCCACHE_DIR=C:\tools\sccache\cache`、`SCCACHE_CACHE_SIZE=15G`。
   - `configure_qet.bat`：加 `-DCMAKE_C_COMPILER_LAUNCHER=sccache -DCMAKE_CXX_COMPILER_LAUNCHER=sccache`。
   - build 是 **Release（無 `/Zi`）** → sccache **不需要** `/Z7`（Debug 才需 `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded`）。
   - 以「前處理後內容」為 key 快取 `.obj`：reconfigure/切分支/清 build 後，內容沒變的檔直接命中、不重編。
   - 指令：`sccache --show-stats`（看命中率）、`--zero-stats`（歸零）、`--start-server`。
2. **PCH 已重新啟用**（移除了 `-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON`）。加速那 ~400 個都 include Qt 標頭的小檔。
3. **版號 define 只綁 `qetversion.cpp`**（`set_source_files_properties(... COMPILE_DEFINITIONS QET_PROJECT_VERSION=...)`）——保留，讓 bump 版號只重編這一個檔，不波及其他。

**實測（2026-07-05）**：

| 情境 | 耗時 |
|---|---|
| 版號 / 小改 1 檔（ninja 自動 reconfigure） | **~21s**（編 1–2 檔 + relink） |
| 乾淨狀態下手動全 `configure_qet.bat` 後 ninja | ~6s（判定 0 檔需編） |
| `ninja -t clean` 後全量重編（sccache **98.6% 命中**） | **~143s**（原 ~14 分，約 6 倍） |
| 冷快取第一次全量（灌快取，一次性） | ~854s（比一般全量還慢，因 sccache 要雜湊+存每個 obj） |

**PCH + sccache 相容嗎？** 相容。clean 全量重編仍 **98.6% 命中**，PCH 沒拖垮快取。當初關 PCH 只是為了躲「reconfigure 全重編」，有 sccache 後那理由消失。

### 重要誤判與真相（別再犯）

1. **「改 CMakeLists / 版號會觸發 KF6 全量重編」——半真半假。**
   真相：**乾淨狀態**下 reconfigure 後 ninja 常判定「0 檔需編」。之前看到的「全量重編（連 SingleApplication/KF6 都重建）」，實際是**我自己砍斷（taskkill）進行中的 build，留下不一致狀態**造成的，不是 reconfigure 本身。
   → 版號/小改動**直接 `ninja`**（走 `build_timed.sh`），讓 ninja 自動做最小 reconfigure；**不要手動跑 `configure_qet.bat` 全配置**（那才會重新產生 autogen/qrc 等）。只有改了 CMake 結構、加解相依時才需手動全 configure。

2. **看到多個 `cl.exe` 就以為「並發 build 互卡」而 taskkill ——錯。**
   單一 ninja build 本來就會開多個 `cl.exe`（= CPU 核心數）平行編譯，那是正常的。曾把一個編到 434/437 快好的 build 誤砍掉，白費一輪。**確認真的有兩個獨立 build 在跑**（例如自己背景重複開了）才清。

3. **用「絕對時間 ×3」當卡死上限 ——會誤砍慢但正常的 build。**
   曾把預期設 150s、硬上限 450s，結果一個正在 299/437 前進中的 build 在 452s 被砍。
   → `build_timed.sh` 已改用**進度停滯偵測**：ninja 的 `[N/M]` 進度行有在變就不砍，只有**連續 180s 沒任何進度且沒有 `cl.exe`** 才判卡死。

4. **正在執行的 shell 腳本，不要編輯。**
   bash 是邊執行邊按 byte offset 讀檔；一個 build 還在跑時去改 `build_timed.sh`，把後段位移改掉 → 該實例 `unexpected EOF` 死掉（ninja 子行程雖仍自行跑完）。要改腳本，等它跑完再改。

5. **全螢幕純色 ≠ app 卡住，是螢幕保護。** 見「驗證」段（截圖前送 Shift 關螢保）。

### 自我監控建置腳本 `_deps/build_timed.sh`

用法：`bash _deps/build_timed.sh <預期秒數> "<說明>"`（建議 `run_in_background`）。它會：
先 taskkill 清殘留確保單一 build → 記時間戳 → 到預期時間仍沒完成就自我診斷（`cl.exe` 數量、是否在重編 KF6、最新進度）→ 進度停滯 180s 判卡死中止 → 結束後自動判定 ✅正常/⚠比預期慢2倍/❌編譯錯誤，並印實際秒數與版本。**每次編譯前先跟使用者報預期秒數，能量就實際 `date` 計時。**

## 驗證

啟動後**須確認真的開出視窗**（主視窗標題 `QElectroTech`），不要只看行程存在。
若跳出「找不到 XXX.dll」的系統錯誤框，它自己也是一個視窗，`MainWindowTitle` 會顯示成
exe 路徑而非 `QElectroTech`——別把錯誤框誤判成 app 成功啟動（見坑 5）。
**另注意螢幕保護**：自動化截圖時久等會被純色螢保蓋住（誤以為 app 卡住/藍屏）；截圖前送真實鍵鼠輸入（如 keybd_event 按 Shift）關掉螢保。

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
