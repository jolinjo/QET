# macOS 建置 / 打包踩坑(給人與 AI)

> 對象:在 macOS 上建置、執行、打包本 fork 的人與 AI。動手前先讀。
> Windows/MSVC 的坑見 `windows-msvc-build-pitfalls.md`。

---

## 1. ⚠️ 絕對不要對 `build/qelectrotech.app` 直接跑 macdeployqt

**這是最容易重犯、且症狀嚇人的坑。**

### 症狀

開發版 app 啟動即崩潰,macOS 跳「qelectrotech 未預期的結束」,按「重新
打開」又立刻再崩,無限循環。用終端機直接跑執行檔會看到:

```
Class QT_ROOT_LEVEL_POOL__… is implemented in both
  /opt/homebrew/Cellar/qtbase/…/QtCore   ← Homebrew 的 Qt
  和  …/build/qelectrotech.app/Contents/Frameworks/QtCore   ← 內嵌的 Qt
…
qt.qpa.plugin: Could not load the Qt platform plugin "cocoa"
This application failed to start because no Qt platform plugin could be
initialized.
```

### 原因

開發版建置(`cmake --build build`)產生的 `build/qelectrotech.app` 是
**輕量 bundle**:裡面只有執行檔與資源,Qt 走 Homebrew(靠 rpath 連到
`/opt/homebrew/opt/qt`)。正常的 `Contents/` 只有 `MacOS/` 和
`Resources/`。

`macdeployqt` 會把整套 Qt frameworks + 平台外掛「複製進 bundle」。一旦
有人**直接對 `build/` 裡的 bundle** 跑它,bundle 內就多了一套 Qt
(`Contents/Frameworks/`、`PlugIns/`、`qt.conf`、`_CodeSignature/`)。
之後 `cmake --build` 只會重連執行檔——執行檔仍指向 Homebrew Qt,但
bundle 內又有第二套 Qt,兩套同時載入 → cocoa 外掛初始化失敗 → 啟動即崩。

### 正確做法

- **OTA 發佈腳本 `scripts/publish_macos_ota.sh` 已經是對的**:它先
  `cp -R build/qelectrotech.app` 到 `/tmp` 的 staging 目錄,只對那份
  **複本**跑 macdeployqt(見腳本第 41–43 行)。跑這支腳本不會污染
  `build/`。**要打包就用這支腳本,不要手動 macdeployqt。**
- 若真的要手動打包,務必先 `cp -R` 到 build 以外的目錄再動手。

### 中鏢了怎麼修

```bash
# 確認 bundle 是否被污染:正常只有 MacOS 和 Resources
ls build/qelectrotech.app/Contents/
# 若看到 Frameworks / PlugIns / qt.conf / _CodeSignature → 已被污染
rm -rf build/qelectrotech.app     # 或改名保留:mv …app …app.deployed-backup
cmake --build build               # 重新產生乾淨的開發版 bundle
```

### 判斷「是舊崩潰殘留還是還在崩」

macOS 的崩潰對話框可能是**先前**崩潰留下的殘影。用時間戳判斷:

```bash
ls -t ~/Library/Logs/DiagnosticReports/ | grep -i qelectrotech | head
```

報告檔名含時間(`qelectrotech-YYYY-MM-DD-HHMMSS.ips`)。若最新一筆早於
你這次重建/啟動的時間,表示現在的 app 其實正常,對話框按「忽略」即可。

---

## 2. 固定淺色主題(不跟隨系統暗色)

QET 的畫布、元件庫、圖框渲染都假設淺色 UI;跟著系統進暗色模式會讓面板
與對話框難以閱讀。因此**強制固定淺色**,做法在 `sources/main.cpp` 的
`applyFixedLightTheme()`:Fusion style + 明確的淺色 QPalette,mac 與
Windows 兩個平台分支都會呼叫。

- `misc/MacOSXBundleInfo.plist.in` 亦設 `NSRequiresAquaSystemAppearance`
  = true,但新版 macOS 常忽略此鍵,**真正生效的是程式碼裡的固定 palette**。
- 要改主體顏色改 `applyFixedLightTheme()` 裡的 palette,不要依賴 OS 主題。

---

## 3. 單一實例機制(為何 macOS 不用 SingleApplication)

`main.cpp` 在 `Q_OS_MACOS` 分支直接用 `QApplication`,不用
`SingleApplication`。因為後者的共享記憶體鎖在 macOS 不可靠(POSIX
segment 遇 crash/kill 殘留;SysV 由 LaunchServices 啟動時被拒),會讓
app 啟動即退出。macOS 的 LaunchServices 本身保證 GUI 單一實例,檔案
開啟走 Apple Events(`MacOSXOpenEvent`)。改這段前先理解這個背景。

---

## 4. 開發建置速記

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/qt;/opt/homebrew/opt/extra-cmake-modules" \
  -DBUILD_KF6=ON
cmake --build build -j
open build/qelectrotech.app      # 或 ./build/qelectrotech.app/Contents/MacOS/qelectrotech 看 stdout
```

- 版號在 `CMakeLists.txt` 的 `VERSION`(每個功能 commit +1);只重編
  `qetversion.cpp` 即生效。
- headless CLI:`--cli-validate / --cli-render / --cli-export-pdf`
  (見 `sources/cli/qetcli.h`)。
