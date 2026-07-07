# QET × Gitea 圖文管理(PDM)開發計畫

> 本文件是給**開發 AI** 看的完整規格:目標、角色流程、技術框架、分期功能、
> 注意事項與驗收條件。動手前請整份讀完,並先完成「開工前必先驗證」一節。

---

## 1. 目標

在本 fork(`QT6-MCP` 分支)的 QET 內,串接**內網 Gitea**,實作公司設備線路
規劃設計流程的圖文管理,達到 PDM 的核心效果:

1. **出庫/入庫**:圖檔獨佔簽出(exclusive checkout),避免兩人同時編輯壞檔。
2. **簽核**:確認者**必須在 QET 內以「瀏覽模式(唯讀)」開圖**檢視後核准或退回。
   單階段簽核——只需**一位確認者**的核准紀錄。
3. **發行**:由**另一位核可放行者**(不能是確認者本人以外還要再簽,單純放行)
   執行真正的發行:合入受保護分支 + 打 tag + 產出發行版 PDF。
4. **帳號管理**:完全沿用 Gitea 帳號與 Team 權限,QET 不自建帳號系統。

### 架構原則(沿用 README)

- QET 只做「殼」:登入、清單、按鈕、狀態顯示。**流程狀態機與權限全部由
  Gitea 原生機制承載**(PR、branch protection、LFS lock、tag、Release)。
- Git 操作一律走 **git CLI 子行程**(QProcess),不引入 libgit2 等重依賴。
  現有「更新公司庫」(blobless + sparse-checkout、按鈕內嵌進度條)的模式
  就是範本,盡量重用。
- 新程式碼集中在 `sources/Pdm/` 新模組,把 fork diff 控制在可維護範圍。

---

## 2. 角色與流程

### 角色(對應 Gitea Team)

| 角色 | Gitea Team | 權限 |
| --- | --- | --- |
| 製圖者 | `pdm-drafters` | repo write(可 push 工作分支、開 PR、LFS lock) |
| 確認者 | `pdm-checkers` | repo write + 列入 branch protection 的核准白名單 |
| 放行者 | `pdm-releasers` | 唯一可 merge 進 `main`、可打 protected tag、建 Release |

同一人可屬多個 Team,但**確認者不得核准自己開的 PR**(Gitea branch
protection 勾選 "Block approval by PR author"... 若該版本無此選項,由 QET
客戶端擋 + webhook 服務複核,見 §7)。

### 圖檔生命週期狀態機

```
 [可出庫] --出庫(lfs lock)--> [編輯中(我鎖定)]
 [編輯中] --入庫(commit+push+unlock)--> [可出庫] 或 --送審--> [審核中(PR open)]
 [審核中] --確認者於 QET 瀏覽模式核准(PR review APPROVE)--> [已確認待發行]
 [審核中] --確認者退回(PR review REQUEST_CHANGES + 意見)--> [編輯中(退回)]
 [已確認待發行] --放行者發行(merge + tag + Release)--> [已發行 vN]
 [已發行] --修訂(從 main 開新分支 + 出庫)--> [編輯中],版次 +1
```

狀態不落任何自建資料庫——全部由 Gitea 現況推導:

- 「編輯中/被誰鎖定」= LFS locks 清單
- 「審核中」= open PR
- 「已確認待發行」= PR 有效核准數 ≥ 1 且未 merge
- 「已發行」= `main` 上的 tag / Release

### 送審與核准的落地方式

- 使用者說的「確認者的 commit」以 **Gitea PR review(APPROVE)** 落地:
  Gitea 會記錄帳號、時間、核准當下的 commit SHA,可追溯性等同一個簽名
  commit,而且 branch protection 能直接強制「無核准不得 merge」。
- 若日後要求更強的形式,可加選項:核准同時由 QET 以確認者身分 push 一個
  空 commit `簽核: <圖號> by <帳號>`(第一期不做)。

---

## 3. 技術框架

### 3.1 儲存與 repo 佈局

- **一台設備 / 一個系統 = 一個 `.qet` 檔**。鎖定粒度是整個檔案,切細才能
  並行作業。禁止全廠一個大 `.qet`。
- repo 切割:**一個產品線(或部門)一個 repo**,repo 內以資料夾分設備。
  Gitea 權限只有 repo 級,存取隔離靠 repo 切割達成。
- `.gitattributes`(必須,進 repo 根目錄):

  ```gitattributes
  *.qet lockable -merge -text
  ```

  - `lockable`:git-lfs 會把未鎖定的 `.qet` 在磁碟上設為唯讀,OS 層面就
    擋掉「沒出庫就改」。
  - `-merge -text`:禁止自動合併與換行轉換。`.qet` 是 XML 但實務上
    **不可三方合併**(座標/uuid/折頁結構),一律視為二進位。
  - 初期 `.qet` 用一般 git 物件儲存(XML 壓縮率好);單檔明顯膨脹
    (內嵌大圖)再改 LFS 儲存。`lockable` 不要求檔案本身存進 LFS。
- 圖號即檔名(命名規範由公司訂),圖號唯一性由 CI 檢查(見 §5 Phase 2)。

### 3.2 Gitea 伺服器設定(開工前由管理員完成)

1. 建立三個 Team(§2)並指派 repo 權限。
2. `main` 分支 branch protection:
   - 禁止直接 push(所有人,含管理員日常操作)
   - Require approvals = 1,核准白名單 = `pdm-checkers`
   - **勾選 "Dismiss stale approvals"**:核准後又 push 新 commit,核准
     自動失效——防止「先給看 A 版、核准後偷換 B 版」。
   - 限制可 merge 者 = `pdm-releasers`
3. Protected tags:pattern `v*` 或 `release/*`,僅 `pdm-releasers` 可建。
4. 啟用 LFS(鎖定功能需要)。
5. (Phase 2 起)架 Gitea Actions runner 於內網,供 PDF 渲染與檔案驗證。

### 3.3 QET 客戶端模組

```
sources/Pdm/
  pdmservice.{h,cpp}        Gitea REST v1 客戶端(QNetworkAccessManager,token 認證)
  pdmgitworker.{h,cpp}      git / git-lfs CLI 包裝(QProcess,非同步,絕不在 UI 執行緒等待)
  pdmdockwidget.{h,cpp}     「圖檔管理」dock:repo/圖檔清單、狀態、出庫/入庫/送審按鈕
  pdmreviewcontroller.{h,cpp} 瀏覽(審核)模式:唯讀開圖 + 核准/退回工具列
  pdmreleasedialog.{h,cpp}  放行者的發行對話框
  pdmsettings.{h,cpp}       設定(伺服器 URL、token、本機工作區根目錄)
```

- **認證**:第一期用 Gitea personal access token(scope 只給 repo 讀寫),
  存 QSettings;Windows 以 DPAPI(`CryptProtectData`)加密後存、macOS 存
  Keychain,至少不落明文。第二期再評估 OAuth2 device/PKCE 流程。
- **本機工作區**:固定根目錄(預設 `文件/QET-PDM/<repo>/`),QET 管 clone
  與更新(blobless clone 加速大 repo)。使用者不需要懂 git。
- **UI 字體**:所有新面板必須接上現有 interface-font 分區機制
  (`applyInterfaceFonts()` / `fontsize_*` 設定),並提供繁中翻譯。
- **版號慣例**:沿用本 fork「每個功能 commit 版號 +1」。

### 3.4 瀏覽(審核)模式——本案關鍵客製

確認者的操作流:PDM 面板「待我確認」清單 → 點一筆 → QET 自動:

1. fetch 該 PR 的 **head commit SHA**,以 detached checkout 放到獨立暫存
   目錄(**絕不動確認者自己的工作區**,也絕不用工作分支最新版——審的必須
   是核准時 API 所綁定的那個 SHA)。
2. 以**唯讀模式**開啟該 `.qet`:
   - QET 對唯讀檔已有處理路徑(檔案系統唯讀時專案會以 read-only 開啟),
     開發時先驗證 `QETProject` 的 read-only 旗標行為,以程式強制設定,
     不依賴檔案屬性。
   - 審核模式下:停用所有編輯 action、隱藏編輯工具列、視窗標題標示
     「審核中(唯讀)- <圖號> @ <SHA 前 8 碼>」。
3. 顯示**審核工具列**:`核准` / `退回(必填意見)` / `開啟 PR 網頁`。
   - 核准 → `POST /repos/{owner}/{repo}/pulls/{index}/reviews`(APPROVE)
   - 退回 → 同 API(REQUEST_CHANGES + 意見文字)
4. 送出後關閉審核視窗、清理暫存 checkout、清單刷新。

### 3.5 發行(放行者)

PDM 面板「待發行」清單(有核准、未 merge 的 PR)→ 發行對話框:

1. 顯示圖號、版次(自動 = 上一 tag +1,可改)、確認者、核准時間。
2. 執行:API merge PR → 建 tag(`<圖號>/vN` 或 repo 級 `vN`,依公司規範)
   → 建 Release。
3. **發行版 PDF 必須由 CI 從 tag 的 commit 渲染**(headless CLI,見 Phase 2),
   不接受任何人本機產的 PDF——保證發行物與入庫內容一致。runner 未就緒前的
   過渡方案:放行者端由 QET 以 tag checkout 後本機渲染再上傳 Release 附件,
   但需在 Release 描述標註渲染來源 SHA。
4. 圖框版次/日期戳印:渲染時由 CLI 參數帶入 tag 版次與發行日期。

---

## 4. 開工前必先驗證(第一個開發 session 要做的事)

寫任何功能前,先用 curl / 臨時腳本對**實際內網 Gitea** 驗證下列各點,並把
結果(版本號、API 行為差異)記錄到本文件附錄:

1. Gitea 版本;LFS 是否啟用;`git lfs lock/unlock/locks` 對 repo 是否可用,
   鎖是否原子(兩客戶端同時鎖同檔,必須恰一個成功)。
2. Branch protection 是否有 "Dismiss stale approvals"、核准白名單、
   "Block approval by PR author"(名稱依版本略異);protected tags 是否支援。
3. PR reviews API(APPROVE / REQUEST_CHANGES)、Releases API(含附件上傳)
   的實際 payload。
4. `lockable` 屬性生效後,Windows 與 macOS 上未鎖檔案是否真的唯讀;
   `git lfs unlock` 後再 lock 的行為。
5. QET `QETProject` 唯讀開啟路徑:找到旗標與生效範圍(哪些編輯入口沒被擋,
   審核模式要補擋哪些)。
6. Windows 客戶端 git + git-lfs 的取得方式:免安裝包(`make_portable.ps1`)
   需一併打包 portable git(含 git-lfs),並驗證 CJK 檔名
   (`core.quotepath=false`)與長路徑(`core.longpaths=true`)。

---

## 5. 開發分期與功能目標

### Phase 0:規範與環境(不寫 QET 程式碼)

- 完成 §3.2 Gitea 設定、§4 驗證,建立一個試點 repo(一條產品線)。
- 訂定:圖號/檔名/資料夾規範、分支命名(`work/<圖號>/<描述>`)、
  tag 規範、`.gitattributes` 範本、repo 範本(可做成 Gitea template repo)。
- **驗收**:純命令列走完一輪完整流程(lock → 改 → push → PR → 核准 →
  merge → tag),確認 Gitea 原生機制承載得住整個狀態機。

### Phase 1:登入 + 出庫/入庫(價值最高)

功能:

- 設定頁新增「圖檔管理」區:伺服器 URL、token(驗證連線按鈕)、工作區路徑。
- 「圖檔管理」dock:repo 清單 → 圖檔清單,每筆顯示狀態
  (可出庫 / 我鎖定 / ○○○鎖定中 / 審核中 / 已發行版次)。
- 出庫:`git lfs lock` + pull 最新 → 直接在 QET 開啟編輯。
- 入庫:存檔 → commit(訊息必填)→ push → `git lfs unlock`;
  「入庫並保持出庫」變體(push 但不解鎖,存進度用)。
- 鎖狀態即時性:面板顯示時與操作前都重新拉 locks 清單;操作失敗
  (被搶鎖、push 被拒)給明確中文訊息與建議動作。

**驗收**:兩台機器兩個帳號,A 出庫後 B 出庫必須失敗且看得到「A 鎖定中」;
A 入庫後 B 可出庫並拿到 A 的版本;全程使用者不接觸 git 指令。

### Phase 2:送審 + 瀏覽模式簽核

功能:

- 製圖者「送審」按鈕:自動開 PR(標題含圖號,描述含變更說明必填),
  送審後該圖對製圖者變為唯讀直到退回或發行。
- 確認者「待我確認」清單 + §3.4 審核模式(唯讀開圖、核准/退回)。
- CI(Gitea Actions):PR 觸發 headless `validate`(檔案可正常載入、
  圖號與檔名一致、圖框欄位齊全),不過則 PR 標紅,QET 端顯示檢查狀態。
- headless CLI:本 fork 原本就規劃的 `render / validate` 在此期落地
  (至少 `validate` + `render` 出 PDF)。

**驗收**:確認者在 QET 內完成一次核准與一次退回;退回意見在製圖者端看得到;
核准後製圖者再 push,核准自動失效(stale dismissal)且 QET 狀態正確回到審核中。

### Phase 3:發行

功能:§3.5 全部;PDM 面板顯示每張圖的已發行版次與發行歷史;
「開啟發行版」= 唯讀開啟 tag 上的檔案(檢視舊版圖)。

**驗收**:放行者一鍵發行後,Gitea 上有 tag + Release + CI 產的 PDF 附件;
非 `pdm-releasers` 成員在 QET 看不到發行按鈕、API 直呼也被 Gitea 拒絕;
發行後修訂流程(從 main 再出庫)可完整走通。

### Phase 4:營運強化(視規模擇項)

- 管理員面板:全域鎖定清單 + 強制解鎖(force unlock,需二次確認並通知原鎖定者)。
- metadata sidecar(`<圖號>.meta.json`:客戶、專案、關鍵字)+ 面板搜尋。
- 視覺差異:審核模式提供「與已發行版並排/疊圖比對」(render 兩版 PNG 疊合)。
- 空 commit 簽名選項(§2)、OAuth2 登入、簽核統計報表。

---

## 6. 注意事項(開發時必須遵守)

1. **沒有鎖就不准編輯**——這是整個方案成立的前提。QET 端任何開啟編輯的
   入口都要檢查鎖;`lockable` 的 OS 唯讀只是第二道保險,不是主防線。
2. **絕不在 UI 執行緒同步等待** git/網路。全部 QProcess/QNetworkReply
   非同步 + 按鈕內嵌進度(沿用公司庫更新的 UI 模式)。
3. **審核對象必須是固定 SHA** 的 detached checkout,與確認者工作區完全隔離。
4. **離線/失敗要可恢復**:push 失敗、鎖 API 逾時、中途斷網,任何一步失敗
   都不能讓本機工作區進入使用者無法自救的狀態;錯誤訊息一律繁中並附
   建議動作。commit 已成但 push 失敗時,提供「重試推送」而不是重做。
5. **不要把流程狀態存在 QET 本機或自建 DB**——一律即時從 Gitea 推導,
   否則多客戶端必然不同步。可以做短 TTL 的顯示快取,但操作前必重新驗證。
6. **fork 可維護性**:新程式碼進 `sources/Pdm/`;對既有檔案的修改以
   「掛接點」為限(選單/dock 註冊、設定頁)。每個功能 commit 版號 +1,
   commit 訊息繁中、說明清楚(沿用現有慣例)。
7. **跨平台**:主要目標 Windows(公司使用)+ macOS(開發機)。Windows
   子行程輸出編碼、CJK 路徑、長路徑問題先讀
   `doc/windows-msvc-build-pitfalls.md`。
8. **token 安全**:不落明文;log 與錯誤訊息絕不可印出 token。
9. **確認者 ≠ PR 作者**:Gitea 設定為主、QET UI 擋為輔;若版本不支援
   該選項,Phase 2 需加 webhook 小服務複核(這是唯一可能需要的自建服務)。
10. **Gitea 備援**:上線前必須有備份排程(dump + repo 目錄)並演練過還原;
    內網單點故障 = 全公司出不了圖。此為管理員責任,但上線 checklist 要列。
11. **發行物不可變**:protected tags + 只有 CI 產的 PDF 才進 Release。
    人為本機 PDF 一律視為非正式。
12. **翻譯與字體**:所有新 UI 進 `lang/` 翻譯流程,接 interface-font 分區。

## 7. 已知風險(設計上接受或已緩解)

| 風險 | 處置 |
| --- | --- |
| 同一 `.qet` 不能兩人並行(鎖粒度=檔案) | 接受;以「一設備一檔」切細緩解 |
| 鎖被休假/離職者卡住 | Phase 4 管理員強制解鎖;過渡期由管理員下 CLI |
| Git 無屬性搜尋 | 命名規範先撐;Phase 4 metadata sidecar |
| Gitea 權限只到 repo 級 | repo 依產品線切割 |
| 簽核法律效力 | 定位為內控紀錄;需正式電子簽章時另案 |
| fork 與上游漸行漸遠 | 模組化隔離 + 本文件記錄全部掛接點 |

---

## 附錄 A:驗證紀錄(§4 完成後由開發 AI 填寫)

(待填:Gitea 版本、API 差異、QETProject 唯讀行為、平台驗證結果)
