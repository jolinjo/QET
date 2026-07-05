# QElectroTech(QT6-MCP fork)

![QElectroTech logo](logo.png)

本 repo 是 [QElectroTech](https://qelectrotech.org/) 的 fork,基於上游
[qelectrotech-source-mirror](https://github.com/qelectrotech/qelectrotech-source-mirror)
的 `qt6_cmake_joshua` 分支(Qt6 + CMake 移植線)。

## 這個 fork 的目的

1. **macOS(Apple Silicon)+ Qt6 可建置可執行**:上游 Qt6 分支在 macOS 上
   無法直接編譯/啟動,本 fork 修掉了全部障礙(見下方修正清單)。
2. **作為 AI 繪圖工具鏈的基座**:配合
   [jolinjo/qet-mcp](https://github.com/jolinjo/qet-mcp) 專案,讓 Claude 等
   AI 透過 MCP 產生與編輯 .qet 電氣圖。原則:**本 fork 改動越少越好**,
   智慧放在外部工具鏈;後續僅計畫新增 headless CLI(render / validate /
   netlist)與極薄的 GUI RPC。

## 主要分支

- `QT6-MCP`:本 fork 的開發主線(預設分支)
- `qt6_cmake_joshua`:上游 Qt6 移植線(同步基準)

## macOS 建置

需求:Homebrew 安裝 `qt`(6.x)、`cmake`、`ninja`、`extra-cmake-modules`、`sqlite`。

```bash
git clone https://github.com/jolinjo/QET.git
cd QET
cmake -S . -B build -G Ninja \
  -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/qt;/opt/homebrew/opt/extra-cmake-modules" \
  -DBUILD_KF6=ON
cmake --build build -j
./build/qelectrotech
```

KF6(kcoreaddons / kwidgetsaddons)與 pugixml、SingleApplication 由 CMake
FetchContent 自動抓取,毋須另外安裝。

## Windows 建置(MSVC)

> **⚠️ 給 AI / 自動化的提示**:上游官方 Windows 流程是 **MinGW**。若改用
> **MSVC(Visual Studio 2022)+ Qt6** 建置,CMake 有數個坑一定會踩到
> (`if(NOT MINGW)` 區塊的空路徑變數、SQLite3 找不到、Git LFS 404、
> FetchContent 外部 repo 授權)。**動手前請先讀**
> [`doc/windows-msvc-build-pitfalls.md`](doc/windows-msvc-build-pitfalls.md)。

## 本 fork 相對上游的修正(macOS/Qt6)

| 版本 | 修正 |
| --- | --- |
| 0.100.1 | Qt6 移植編譯錯誤:`QWheelEvent::delta()`、`qHash` 歧義、`qsizetype` 窄化 |
| 0.100.2 | 介面語言不生效(翻譯改由內嵌資源 `:/lang/` 載入)、`QSignalMapper` 訊號改名 |
| 0.100.3 | `QSignalMapper` QWidget 對應改走 `mappedObject` + functor connect |
| 0.100.4 | macOS 單一實例殘留鎖(POSIX shm 不釋放)改用 SysV,crash 後可自動恢復 |
| 建置修補 | ECM 6.27 `ECMGenerateQDoc` 重複 target(CMP0002)以本地修補版 ECM 解決;SingleApplication 於 configure 時套用 SysV patch |

## 公司版客製(Windows / 繁體中文)

`QT6-MCP` 分支在 Windows/MSVC 上另做了一系列繁中在地化與公司流程客製
(版號 0.100.5 起持續遞增,每個功能 commit +1)。重點:

### 繁體中文在地化

- 預設語言 `zh_TW`;語言選單用台灣國旗 +「繁體中文」。
- UI 字體內嵌 **文泉驛微米黑**(小字清晰)、圖面字體 **YaHei Consolas**,皆編進 exe。
- 偏好設定可調**全域界面字體大小**與**分區大小**(選單/工具列/專案面板/元件庫/屬性)。
  標準對話框(QMessageBox 等)字體跟隨全域大小。
- 內嵌 `qtbase_zh_TW.qm`,標準對話框按鈕顯示繁中(儲存/取消…);存檔對話框的
  「丟棄」改顯示「直接關閉」。
- 面板標籤:內建圖框→**使用圖框**、公司圖框→**公司圖框範本**;專案標題只顯示**檔名**。

### 公司元件庫 / 圖框同步

- 元件庫面板「**更新公司庫**」按鈕:從 Git 倉庫(預設 `github.com/jolinjo/QET-Lib`,
  偏好設定可改)以 **blobless + sparse-checkout** 快速列出線上庫與版本,勾選要更新的
  項目後下載。進度以**按鈕下方內嵌進度條**顯示(不跳對話框);下載完自動清
  `ElementPictureFactory` 快取並重載,更新即時生效。
- 偏好設定可**隱藏 QET 內建元件庫/圖框**(只顯示公司/使用者);公司圖框節點預設收合。
- 新增專案預設頁面 16 欄×100px、10 列×101px;預設圖框範本用公司範本。

### 介面精簡 / 修正

- 偏好設定移除:各集合/圖框/模組目錄、元件管理段、視窗/分頁模式、HDPI 捨入策略
  (皆用預設值,不需使用者調整)。
- 工具列 text-under-icon 文字**底部對齊**(`ToolBarBottomTextStyle` QProxyStyle,
  不受混合尺寸 PNG 圖示高度影響)。
- 元件庫搜尋支援**中文**(含 CJK 字元時放寬單字即可搜,原限 3 字元擋掉中文詞)。

### 免安裝 / 零殘留

- `--config-dir` 時設定改寫本機 INI(非登錄檔);打包腳本 `_deps/make_portable.ps1`
  產生完全免安裝、零殘留、含完整元件庫的可攜資料夾。詳見採坑指南。

> 建置環境、加速(sccache/PCH)、Windows 腳本編碼等踩坑見
> [`doc/windows-msvc-build-pitfalls.md`](doc/windows-msvc-build-pitfalls.md)。

## 上游資訊

- QElectroTech 是自由開源的電氣圖 CAD/CAE 繪圖軟體(無模擬/計算功能,專注繪圖)
- 授權:[GNU GPL v2+](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
- [官網](https://qelectrotech.org/)、[Forum](https://qelectrotech.org/forum/index.php)、
  [Wiki](https://qelectrotech.org/wiki_new/)、
  [Bugtracker](https://qelectrotech.org/bugtracker/my_view_page.php)、
  [Doxygen 文件](https://qelectrotech.github.io/qelectrotech-source-mirror/)
- 上游原始碼(含 submodules):

```bash
git clone --recursive https://github.com/qelectrotech/qelectrotech-source-mirror.git
```
