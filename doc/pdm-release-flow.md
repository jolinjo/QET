# PDM 核准發行流程:按下「核准發行」後系統做了什麼

> 對象:維護者。逐步記錄「核准發行」按下後的完整程式路徑,以及每一步跟
> Gitea 的交握。相關:`pdm-gitea-dev-plan.md`、`pdm-gitea-admin-guide.md`。
>
> 進入點:`PdmDialog::approveAndRelease()`(`sources/Pdm/pdmdialog.cpp`)。

## 交握型態圖例

- **本機** — 只動本機 git / 檔案,不連 Gitea。
- **⇄ git** — git-over-HTTP,跟 Gitea 走 git 協定(fetch / push / ls-remote / pull)。
- **🌐 REST** — Gitea REST API(`/api/v1/...`)。

## 名詞

- **vault** — 本機圖庫工作區(clone),永遠停在 `main`,是渲染 PDF 的正本來源。
- **work 分支** — `work/<圖號>`,單一圖檔的編輯線,送審時對應一個 PR。
- **worktree** — vault 之外、掛在 work 分支上的獨立工作區,簽核 commit 都在此進行。

---

## 階段 0 — 前置(本機,無網路)

1. `busyGuard()` 擋併發操作;取選取圖檔的 `pr_index`。
2. 跳輸入框要**簽核意見**(必填,空白即中止)。
3. 算下一個主版號 `nextMajor(state.revision)`:0.x→**1.0**、1.x→**2.0**、4.x→**5.0**。

## 階段 1 — 在 work 分支寫「核准發行」commit(`signoffOnWorkBranch`)

4. ⇄ git `git fetch origin --prune`(vault)——拉最新 refs。
5. 本機把 worktree 對齊遠端:`git checkout work/<圖號>` → `git reset --hard
   origin/work/<圖號>`(worktree 不存在則 `git worktree add --track -b …`)。
6. 本機 `stampDocFields`:把 **doc-status=「正式發行 Released」、approved-by=你、
   版本=1.0**(階段 0 算出的主版)寫進 .qet 圖框 XML。
7. 本機 `git commit -m "核准發行 by X / 意見：…" -- <檔>`。
   **這步失敗(且非 nothing to commit)整個中止**——絕不拿未戳記內容去合併。
8. ⇄ git `git push origin work/<圖號>`——把這顆正式發行 commit 推上 Gitea。

## 階段 2 — 核准 + 合併(`after_push` → `merge_and_release`)

9. 🌐 REST `POST /repos/{repo}/pulls/{pr}/reviews`,body `{event: APPROVED,
   body: 意見}`。
   - 開發期單人測試會撞 Gitea「不能核准自己的 PR」→ 回 **422**,程式**跳過核准
     直接合併**(靠 main 分支保護暫免必要核准)。正式多人環境由他人核准即正常。
10. 本機 `git rev-parse HEAD`(worktree)→ 取剛推那顆 commit 的 **SHA**。
11. ⇄ git `git ls-remote --tags origin refs/tags/release/<圖號>-v*`——算出下一個
    `release/<圖號>-vN`。
12. 🌐 REST `POST /repos/{repo}/pulls/{pr}/merge`,body:
    ```json
    { "Do": "merge",
      "delete_branch_after_merge": true,
      "head_commit_id": "<步驟 10 的 SHA>" }
    ```
    - `head_commit_id`:**Gitea 只在 PR head == 此 SHA 時才合併**,否則回錯——
      消除「push 已更新但 Gitea 尚未索引 PR head」的競態(舊 bug 就是這樣把
      編輯中內容併進 main)。
    - 失敗**每 1 秒重試最多 3 次**(等 Gitea 算 mergeable)。
    - `delete_branch_after_merge` → **Gitea 端刪掉遠端 work 分支**。
13. 合併成功後把 vault 對齊新的 main:⇄ git `git fetch origin --prune` →
    `git checkout main` → `git pull --ff-only` → 本機 `git rev-parse HEAD`。

## 階段 3 — 產 PDF + 建 Release(`finishRelease`)

14. 本機子行程:`qelectrotech --cli-export-pdf <vault/檔> --out <tmp.pdf>`——
    **從 vault(= 已合併的 main 內容)**渲染,不用任何人的工作區。渲染失敗
    **不擋發行**(tag 內容才是正本),只在說明註記「PDF 渲染失敗」。
15. 🌐 REST `POST /repos/{repo}/releases`,body `{tag_name: release/<圖號>-vN,
    target_commitish: main, name, body}`——**Gitea 會順便在 main 打出該 git tag**。
16. 有 PDF → 🌐 REST `POST /repos/{repo}/releases/{id}/assets?name=…` 上傳附件。

## 階段 4 — 清理(本機)

17. `git worktree remove --force <worktree>` + `git branch -D work/<圖號>`——
    清本機 work 工作區與分支(遠端分支已在步驟 12 被刪)。
18. `refresh()`——重讀清單/角色,狀態列顯示「『圖號』已發行:release/<圖號>-vN」。

---

## 跟 Gitea 交握總清單

| # | 型態 | 內容 |
|---|---|---|
| 4 | ⇄ git | fetch 最新 refs |
| 8 | ⇄ git | push 核准發行 commit 到 work 分支 |
| 9 | 🌐 REST | 送核准 review(APPROVED) |
| 11 | ⇄ git | ls-remote 查發行 tag 版次 |
| 12 | 🌐 REST | merge PR(帶 head_commit_id + 刪遠端分支) |
| 13 | ⇄ git | fetch / pull 對齊 main |
| 15 | 🌐 REST | 建 Release(順便打 tag) |
| 16 | 🌐 REST | 上傳 PDF 附件 |

## 兩道資料完整性防呆(務必保留)

- **階段 1「commit 失敗就中止」**:保證併進 main 的一定是戳記過的正式版。
- **步驟 12 的 `head_commit_id`**:保證併的是剛推那顆,不會誤併舊 head。

這兩點是先前「發行卻併到編輯中內容」bug 修掉的根因,改動發行流程時不可移除。
