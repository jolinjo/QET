# PDM 管理員手冊:Gitea 設定與帳號管理

> 對象:負責內網 Gitea 與 QET 圖檔管理(PDM)的系統管理員。
> 使用者操作見 `pdm-user-tutorial.md`;架構與開發背景見 `pdm-gitea-dev-plan.md`。
> 本手冊以 **Gitea 1.24.x** 為準(功能已對 1.24.7 全數實測)。

---

## 1. 伺服器需求

- Gitea 1.24 以上,**必須啟用 LFS**(`app.ini` 的 `[server]`
  `LFS_START_SERVER = true`,預設多半已開)。圖檔鎖定(出庫)靠 LFS
  file locking,LFS 沒開整套機制就不成立。
- 主機建議沿用現有 `http://hc-server:3000`。
- (選配,建議)Gitea Actions runner 一台:供 PR 自動檢查圖檔
  (`--cli-validate`)與發行 PDF 渲染;沒有 runner 系統一樣能用,
  PDF 改由放行者的 QET 用戶端渲染上傳。

## 2. 帳號管理

### 2.1 原則

- **QET 不自建帳號**:誰能登入、誰有什麼權限,完全等於 Gitea 上的
  帳號與 Team 成員資格。到職開 Gitea 帳號、離職停用 Gitea 帳號,
  PDM 權限就同步生效/失效。
- 每人一個帳號,**禁止共用**——簽核紀錄的意義建立在帳號=本人。

### 2.2 建立組織與三個 Team

建一個組織(例如 `pdm`),之下三個 Team,權限如下:

| Team | 角色 | Gitea 權限設定 |
| --- | --- | --- |
| `pdm-drafters` | 製圖者 | Write(可 push、開 PR、鎖定檔案) |
| `pdm-checkers` | 確認者 | Write + 列入分支保護「核准白名單」 |
| `pdm-releasers` | 放行者 | Write + 列入「可合併白名單」與保護 tag 白名單 |

- 三個 Team 都勾「存取所有倉庫」(includes all repositories),
  之後新增產品線 repo 不用逐一調整。
- 同一人可加入多個 Team(例如資深工程師兼確認者);
  **PR 作者不能核准自己的 PR** 是 Gitea 原生行為,不用另外設。

### 2.3 使用者加入流程(到職)

1. Gitea 管理後台建帳號(或開 LDAP/SSO,若公司有)。
2. 把帳號加入對應 Team。
3. 使用者自己到 Gitea 網頁:右上頭像 → 設定 → 應用程式 →
   產生新 token,權限勾 **repository(讀寫)**、**user(唯讀)**、
   **organization(唯讀)**。
4. 使用者在 QET:偏好設定 →「圖檔管理」→ 填伺服器網址與 token →
   「驗證連線」顯示帳號名即完成。

### 2.4 離職/停權

- Gitea 停用該帳號(或撤銷其 token)即全面失效。
- **先檢查他有沒有鎖定中的圖**:repo 頁面或請任一管理員在 QET 面板
  查看「鎖定者」欄;有的話依 §5 強制解鎖流程處理,否則圖會卡死。

## 3. 圖檔倉庫設定(每個產品線一個 repo)

### 3.1 建立 repo

- 在 `pdm` 組織下建 repo(私有),命名照產品線(例如 `pilot-line`)。
- 預設分支 `main`。
- repo 根目錄必須有 `.gitattributes`,內容:

```gitattributes
*.qet lockable -merge -text
```

(禁止 git 自動合併 .qet、標記為可鎖定。建議做一個 template repo,
之後新產品線直接由範本建立。)

### 3.2 main 分支保護(核心,漏設等於沒有簽核)

repo 設定 → 分支 → 新增規則,分支名 `main`:

| 設定項 | 值 | 目的 |
| --- | --- | --- |
| 停用推送(Disable push) | ✅ | 任何人都不能直接改 main,只能走 PR |
| 所需核准數(Required approvals) | 1 | 一位確認者核准即可 |
| 限制核准白名單 | Team `pdm-checkers` | 只有確認者的核准算數 |
| 廢止過時核准(Dismiss stale approvals) | ✅ **必開** | 核准後又推新 commit,核准自動失效,防「看 A 版核准、偷換 B 版發行」 |
| 限制可合併白名單 | Team `pdm-releasers` | 只有放行者能執行發行(合併) |

### 3.3 保護 tag(發行版不可竄改)

repo 設定 → 標籤保護:pattern `release/*`,白名單 Team `pdm-releasers`。
發行 tag 只有放行者(透過 QET 發行功能)能建立,任何人不得刪改。

### 3.4 圖檔命名

- **圖號即檔名**(`<圖號>.qet`),一台設備/一個系統一個檔——鎖定
  粒度是整個檔案,切太大會讓多人無法並行作業。
- 檔名避免空白(QET 會自動把空白轉 `-` 當分支名,但源頭就規範好最乾淨)。
- 圖檔第一次進系統:由製圖者放進出庫工作區走一輪「入庫→送審→發行」,
  或管理員直接以 PR 匯入既有圖面。

## 4. 例行維運

### 4.1 備份(上線前必須就緒並演練過)

Gitea 全量備份 = `gitea dump` + LFS 資料目錄。Docker 部署範例:

```bash
docker exec -u git <容器名> gitea dump -c /data/gitea/conf/app.ini
# dump 檔在容器 /data 下;連同 volume 一併排程備份到異地
```

- 建議每日排程 + 保留 30 天;**至少演練過一次還原**。
- 內網 Gitea 是單點:它掛掉全公司無法出庫/發行(已出庫者可繼續
  編輯本機工作區,復原後再入庫)。

### 4.2 升級

- 升級 Gitea 前先看 release notes 的 LFS/API 變更;
  升級後用 `doc/pdm-gitea-dev-plan.md` 附錄 A 的清單快速回歸一次
  (鎖定原子性、核准失效、merge 白名單、保護 tag)。

## 5. 卡鎖處理(強制解鎖)

情境:同仁休假/離職/電腦故障,圖鎖著沒人能出庫。

1. 先確認本人確實無法自行解鎖(聯絡得上就請他在 QET 按「取消出庫」)。
2. 管理員在 QET 選該圖 →「強制解鎖…」→ 閱讀警告後確認。
   (等效指令:在該 repo 的 clone 內 `git lfs unlock --force <檔名>`。)
3. 通知原鎖定者:他工作區裡未入庫的修改**不會遺失**,但已無法直接
   入庫;需要時由管理員協助從他的工作區手動救回。

> ⚠️ **重要限制**:Gitea 的 force unlock 只需要 repo **write 權限**
> (原生行為,無法設定限縮)。技術上任何製圖者都做得到——QET 介面
> 有強確認對話框防誤觸,但**制度上必須明訂:強制解鎖僅限管理員執行**,
> 所有解鎖操作 Gitea 都有紀錄可稽核,違規可追查。教育訓練要講清楚。

## 6. (選配)Gitea Actions:PR 自動檢查與發行渲染

有 runner 之後建議補上,取代/加強用戶端渲染:

- **PR 觸發**:對 PR head 跑 `qelectrotech --cli-validate <改動的.qet>`,
  失敗即標紅——壞檔在人工審核前就被擋下。
- **發行 tag 觸發**:`--cli-export-pdf` 渲染 PDF 附到 Release,
  保證發行物 100% 出自 tag 內容。
- runner 需要能跑 QET 的環境(Linux 容器需 Qt6 執行庫與字型;
  或用 Windows runner 跑打包好的免安裝版)。

## 7. 快速檢核表(新環境部署)

- [ ] Gitea LFS 已啟用
- [ ] 組織 + 三個 Team 建立,成員就位
- [ ] repo 由範本建立,`.gitattributes` 就位
- [ ] main 分支保護五項全設(§3.2,特別是 Dismiss stale)
- [ ] 保護 tag `release/*` → `pdm-releasers`
- [ ] 每日備份排程 + 已演練還原
- [ ] 全員 token 發放、QET 驗證連線成功
- [ ] 用測試圖走一輪完整流程(出庫→入庫→送審→核准→發行)
- [ ] 強制解鎖制度公告 + 教育訓練
