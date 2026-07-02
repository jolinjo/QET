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

## 本 fork 相對上游的修正(macOS/Qt6)

| 版本 | 修正 |
| --- | --- |
| 0.100.1 | Qt6 移植編譯錯誤:`QWheelEvent::delta()`、`qHash` 歧義、`qsizetype` 窄化 |
| 0.100.2 | 介面語言不生效(翻譯改由內嵌資源 `:/lang/` 載入)、`QSignalMapper` 訊號改名 |
| 0.100.3 | `QSignalMapper` QWidget 對應改走 `mappedObject` + functor connect |
| 0.100.4 | macOS 單一實例殘留鎖(POSIX shm 不釋放)改用 SysV,crash 後可自動恢復 |
| 建置修補 | ECM 6.27 `ECMGenerateQDoc` 重複 target(CMP0002)以本地修補版 ECM 解決;SingleApplication 於 configure 時套用 SysV patch |

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
