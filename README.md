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
   AI 透過 MCP 產生與編輯 .qet 電氣圖。**架構原則(僅針對 AI/MCP 介面層)**:
   繪圖「智慧」放在外部工具鏈,QET 只提供穩定的資料與 RPC 介面;此線後續計畫
   新增 headless CLI(render / validate / netlist)與極薄的 GUI RPC。

   > ⚠️ 這條「改動越少越好」**只適用於 MCP 介面的智慧分工**,不是對整個 repo 的
   > 限制。**讓 QET 本身更好用、更快、更穩定的改動是歡迎且鼓勵的**(UI/在地化/
   > 效能/穩定性皆可),任何這類發現都可以提出來討論並修改。

## 主要分支

- `QT6-MCP`:本 fork 的開發主線(預設分支)
- `qet-pdm`:圖檔管理(PDM,串接 Gitea)功能開發線
- `qt6_cmake_joshua`:上游 Qt6 移植線(同步基準)

## 圖檔管理(PDM)— 串接 Gitea 的圖文管理

`qet-pdm` 分支(版本 1.0.23 起)在 QET 內建了一套完整的圖檔生命週期
管理,後端只用內網 Gitea 的原生機制(LFS 檔案鎖、Pull Request、
branch protection、tag/Release),不需要另架任何伺服器或資料庫。

### 解決什麼問題

- **兩人同時改同一張圖互相覆蓋**:出庫 = 獨佔鎖定,同一時間只有一個人
  能編輯;其他人看得到「誰鎖定中」。
- **版本混亂、不知道哪份是正式版**:所有歷史都在 Git;正式版一律走
  「送審 → 確認 → 發行」產生 tag 與 PDF,發行版不可再改。
- **簽核無紀錄**:確認者在 QET 內以唯讀模式看圖後核准/退回,
  Gitea 記錄帳號、時間與核准的確切版本(commit SHA),可完整稽核。

### 流程與角色

```text
製圖者:出庫(鎖定) → 編輯 → 入庫(推送) → 送審(開 PR)
確認者:審核檢視(唯讀開圖) → 核准 或 退回(附意見)
放行者:發行(合入 main + tag + 自動產 PDF 附件)
```

三種角色對應 Gitea 的三個 Team(`pdm-drafters` / `pdm-checkers` /
`pdm-releasers`),帳號與權限完全由 Gitea 管理,QET 不自建帳號系統。
同一人可兼多個角色,但**不能核准自己送審的圖**(Gitea 原生強制)。

### 主要機制

- **出庫/入庫**:`git lfs lock` 獨佔鎖 + 每張圖一條 `work/<圖號>` 分支;
  本機採 vault(主 clone)+ git worktree(每張出庫圖獨立工作區)模型,
  使用者全程不需要懂 git。
- **審核**:抓 PR head 的固定 commit 到獨立唯讀工作區開圖——審的
  永遠是送審那一版;核准前會再比對 SHA,送審後偷改會被擋下。
- **發行**:版次自動遞增(`release/<圖號>-vN`),PDF 由內建 headless CLI
  (`--cli-export-pdf`)從庫內容渲染,不收任何人本機自產的檔案。
- **狀態即時**:面板顯示每張圖「可出庫/編輯中/出庫中/已入庫未送審/
  審核中/已確認待發行」,全部即時從 Gitea 推導,無本機快取狀態。

### 使用與部署文件

| 文件 | 對象 |
| --- | --- |
| [doc/pdm-user-tutorial.md](doc/pdm-user-tutorial.md) | 一般使用者:三種角色的操作教學 |
| [doc/pdm-gitea-admin-guide.md](doc/pdm-gitea-admin-guide.md) | 管理員:Gitea 伺服器設定與帳號管理 |
| [doc/pdm-gitea-dev-plan.md](doc/pdm-gitea-dev-plan.md) | 開發者:架構、分期、驗證紀錄 |

QET 端設定只有三格:偏好設定 →「圖檔管理」→ 填 Gitea 伺服器網址、
access token、本機工作區路徑,按「驗證連線」成功即可使用左側
「圖檔管理」面板。

### 已知限制(v1.0.24)

- headless CLI 的 CI 自動檢查(PR 觸發 validate)需要內網 Gitea Actions
  runner,尚未部署;目前由審核者在 QET 內實際開圖把關。
- Windows 免安裝包尚未內建 git/git-lfs(部署前要補);token 加密儲存
  目前僅 macOS(鑰匙圈),Windows DPAPI 待做。
- Gitea 的強制解鎖只需 write 權限(原生行為),靠確認對話框與稽核
  紀錄防呆,詳見管理員文件。

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
