/*
	Copyright 2026 QElectroTech Team
	This file is part of QElectroTech.

	QElectroTech is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	QElectroTech is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with QElectroTech.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef PDMDIALOG_H
#define PDMDIALOG_H

#include "pdmrevision.h"

#include <QDialog>
#include <QHash>
#include <QMap>
#include <QStringList>

#include <functional>

class PdmGitWorker;
class PdmService;
class QComboBox;
class QGroupBox;
class QDomDocument;
class QLabel;
class QProgressBar;
class QPushButton;
class QShowEvent;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

/**
	@brief 「圖檔管理」彈出視窗(PDM)。
	串接 Gitea 實作完整圖檔生命週期:
	出庫(獨佔鎖) → 入庫(推送 work 分支) → 送審(開 PR) →
	審核(唯讀開圖+核准/退回) → 發行(merge+tag+Release+PDF 附件)。
	本機採 vault + worktree 模型,見 doc/pdm-gitea-dev-plan.md §3.4。
*/
class PdmDialog : public QDialog
{
	Q_OBJECT

	public:
		explicit PdmDialog(QWidget *parent = nullptr);

		/// 某開啟中的 .qet 是以何種圖檔管理情境開啟(供工具列切換)
		enum OpenContext { NotManaged, CheckoutEdit, ReviewReadOnly };
		OpenContext openContext(const QString &abs_path) const;
		bool isConfirmer() const { return m_is_confirmer; }
		bool isReleaser() const  { return m_is_releaser; }
		/// 該 abs_path 是否為「我出庫中(已鎖定、尚未入庫)」的檔
		bool isCheckedOutByMe(const QString &abs_path) const;
		/// 該 .qet 是否由圖檔管理開啟(在 PDM 工作根目錄下,或唯讀檢視暫存匯出)
		static bool isManagedPath(const QString &abs_path);
		/// 由本機開檔路徑反推圖檔在圖庫的相對路徑(專案資料夾/檔名);
		/// 工作區內直接反推,發行版/最新版暫存則以檔名比對清單。空=無法解析。
		QString drawingRelPath(const QString &abs_path) const;

	signals:
		/// 要求編輯器開啟一個 .qet 檔(出庫/審核檢視時發出)
		void requestOpenFile(const QString &file_path);
		/// 要求編輯器關閉某 .qet(入庫/取消出庫後,避免編輯器留著舊內容)
		void requestCloseFile(const QString &file_path);
		/// 要求編輯器存檔某 .qet(入庫前自動存檔,免使用者手動 Cmd+S)
		void requestSaveFile(const QString &file_path);
		/// 背景連線結果:ok=已連上並取回資料(供工具列按鈕 enable/disable)
		void connectionReady(bool ok);
		/// 按「新增圖檔」:請編輯器提供目前開啟的圖檔(存檔後回呼 addDrawingFromFile)
		void requestAddCurrentDrawing();

	public slots:
		void refresh();
		/// 程式啟動後背景先連一次(不需開視窗),結果由 connectionReady 發出
		void startBackgroundConnect();
		/// 把指定來源 .qet 加入圖庫(由編輯器提供目前開啟檔的路徑後呼叫)
		void addDrawingFromFile(const QString &source_path);
		// 由工具列針對「某開啟中的檔」直接執行動作(先在清單選到該檔再動作)
		void checkInByPath(const QString &abs_path);
		/// 入庫並直接送審(一步):存檔→戳記→commit→push→開 PR
		void submitDirectByPath(const QString &abs_path);
		/// 出庫並編輯該檔;清單未載入或非工作區路徑時改開圖檔管理視窗
		void checkOutByPath(const QString &abs_path);
		/// 唯讀瀏覽該圖的「伺服器最新版」:fetch 後取 origin/main 內容存暫存
		/// 唯讀開啟,不會打開本機舊快取(清單未載入時改開圖檔管理視窗)
		void browseLatestByPath(const QString &abs_path);
		void cancelByPath(const QString &abs_path);
		void confirmByPath(const QString &abs_path);
		void releaseByPath(const QString &abs_path);

	protected:
		void showEvent(QShowEvent *event) override;

	private:
		struct FileState {
			QString rel_path;      ///< 相對 vault 的路徑
			QString lock_owner;    ///< 空 = 未鎖定
			bool has_work_branch = false;
			int pr_index = 0;      ///< 0 = 無開啟中 PR
			QString pr_author;
			QString pr_head_sha;
			bool pr_approved = false;
			QString revision;      ///< 工作小版(讀自專案級 pdm_work_version;舊檔退回首頁 indexrev)
			QString doc_status;    ///< 文件狀態(讀自 .qet 的 doc-status)
			QString drawn_by;      ///< 繪製者(讀自 .qet 的 author 屬性)
			QString checked_by;    ///< 審核者(讀自 .qet 的 checked-by)
			QString approved_by;   ///< 核准者(讀自 .qet 的 approved-by)
			QString work_author;   ///< work 分支末次 commit 作者 email
		};

		void setUpWidget();
		void connectionRefreshed();
		void syncRepository();
		void loadFileStates();
		/// 檔案清單完成「發現」(main + work 分支上的新檔)後,套用鎖定/
		/// 作者/work 分支內容等中繼資料,再讀 PR。
		void applyFileMetadata(const QString &vault);
		void loadPullRequests();
		void rebuildTree();
		void rebuildFolderTree();
		void populateFileList();
		void updateButtons();
		/// 選取圖檔後自動載入其發行歷史到右側面板
		void loadReleaseHistory(const QString &rel_path);
		/// 唯讀開啟某個發行 tag
		void openReleaseRevision(const QString &tag, const QString &rel_path);

		/// 讀 .qet 首頁圖框欄位(版本/文件狀態/繪製者/審核者/核准者)填入 state
		void readDocFields(const QString &abs_path, FileState *state) const;
		/// 從已解析的 XML 取首頁圖框欄位填入 state(供讀 work 分支內容用)
		void parseDocFields(const QDomDocument &doc, FileState *state) const;
		/// 把文件狀態/修訂索引/附加欄位寫入 .qet 每一頁圖框並存檔。
		/// status 空字串=不動狀態;set_revision=true 時才寫 revision
		///(可為空字串以清空版本),false 則保留原修訂索引;
		/// extra_fields 內每個 name→value 會寫入對應附加欄位(如
		/// checked-by/approved-by),value 可為空字串以清空。
		bool stampDocFields(const QString &abs_path, const QString &status,
				    const QString &revision, bool set_revision,
				    const QMap<QString, QString> &extra_fields =
					    QMap<QString, QString>()) const;
		// 版本進版邏輯已移至 PdmVersion::nextMinor / nextMajor

		/// 依目前發行史(含送審時的待發行列)把首頁的 HTML 表格重繪到
		/// 文件管制頁。送審與核准發行都用它。@a doc 會被就地修改。
		void renderReleaseTable(QDomDocument &doc) const;

		/**
			入庫/送審前的變更說明對話框:列出本檔各頁修訂欄的修訂項
			供勾選,並收「變更摘要」(必填)與「出入庫意見」(選填)。
			@param abs_path 已存檔的工作區 .qet(呼叫前須先 requestSaveFile)
			@param commit_message 回填:結構化 commit(含 pdm-meta,供 commit)
			@param human_summary 回填:純人類摘要(供 PR body/顯示)
			@return false = 使用者取消或摘要空白
		*/
		bool promptCheckinCommit(const QString &abs_path,
					 const QString &title,
					 const QString &summary_label,
					 QString *commit_message,
					 QString *human_summary,
					 bool require_changes = false);

		/// 送審類流程用:檢查該檔是否有「待發行修訂項」(核准者為空者);
		/// 沒有則跳詳細說明並回 false(呼叫端據此中止送審)。@a doc 為已載入
		/// 的 .qet 文件。
		bool ensureHasPendingChanges(const QDomDocument &doc,
					     const QString &title);

		/// 檢查修訂列是否有「填一半」(有日期/座標卻缺修改內容,或反之)。
		/// 有缺漏則跳訊息列出並回 false(呼叫端據此中止送審)。
		bool ensureRevisionRowsComplete(const QDomDocument &doc,
						const QString &title);

		/**
			發行前:放行者從「自上次發行後的增修項」中勾選要寫入首頁
			發行史的項目,並填簽核訊息(必填)。
			@param entries 候選增修項(collectChanges 過濾後)
			@param chosen  回填:勾選的項目
			@param message 回填:簽核訊息
			@return false = 取消或訊息空白
		*/
		bool promptReleaseSelection(
			const QList<PdmRevision::Entry> &entries,
			QList<PdmRevision::Entry> *chosen, QString *message);

		void addNewDrawing();   ///< 新增一張圖檔到圖庫(建 work 分支、出庫編輯)
		void checkOut();
		void checkIn();
		void cancelCheckOut();
		void submitForReview();
		/// 入庫送審:存檔→戳「審核中」→單一 commit(內容+狀態,少一次 commit)
		/// →push→解鎖→開 PR。供出庫編輯中的檔一步完成入庫並送審。
		void checkInAndSubmit();
		void openReviewView();
		void viewReleased();   ///< 唯讀開啟 main 上最後發行版
		void confirmDone();    ///< 確認者「確認完畢」:寫確認者+commit
		void rejectReview();   ///< 退回:寫退回狀態+commit+REQUEST_CHANGES
		void approveAndRelease(); ///< 核准者「核准發行」:寫核准者+發行
		void finishRelease(const QString &rel_path, const QString &stem,
				   const QString &tag, const QString &sha);
		void forceUnlock();
		/// 退回上一發行版:捨棄進行中的 work 分支,還原為 main 最後發行版
		void revertToRelease();

		// 結構性維運(直接改 main,限核准者且 main 開放其推送):
		void addFolder();       ///< 新增專案資料夾(.gitkeep 佔位)
		void deleteFolder();    ///< 刪除選取資料夾及其下所有圖檔
		void deleteDrawing();   ///< 刪除選取圖檔
		/// 在 vault 的 main 上做結構性變更:準備→change(內含 git add/rm)→
		/// commit(訊息)→push→重整
		void mutateMain(const std::function<void ()> &change,
				const QString &commit_message);

		/// 查登入者所屬 team,設定 m_is_confirmer / m_is_releaser
		void loadUserRoles(int retries_left = 2);
		/**
			在 work 分支上簽核:準備 work 分支工作區→戳記(狀態/附加
			欄位)→commit(帶簽核訊息)→push,成功後呼叫 after_push。
			供確認/核准/退回共用(它們都要在 work 分支留 commit)。
		*/
		void signoffOnWorkBranch(const QString &rel_path,
			const QString &status,
			const QMap<QString, QString> &extra_fields,
			const QString &commit_message,
			const std::function<void ()> &after_push,
			bool set_revision = false,
			const QString &revision = QString(),
			int progress_steps = 5,
			// 戳記圖框後、commit 前的額外檔案編輯(發行時用來 append
			// 發行史列);回 false 代表失敗、中止。null=不做。
			const std::function<bool (const QString &abs_path)>
				&post_stamp = nullptr);
		/// op_steps=該操作預期的 git 步數(進度條以 已完成/預期 顯示;超出預期
		/// 的尾段背景重整維持 100%)。0=背景讀取,用漸進逼近。
		void showBusy(bool busy, bool with_dialog = true, int op_steps = 0);
		/// 已有使用者操作進行中(進度框顯示中)則擋下新操作,回 true。
		/// 防止兩個操作的 git(尤其 reset --hard)並行互相破壞 staging。
		bool busyGuard();
		/// 建立(僅一次)專用進度對話框
		void ensureBusyDialog();
		/// 把 git/子行程指令對應成使用者看得懂的階段說明
		static QString friendlyStep(const QString &cmd);
		void fail(const QString &title, const QString &log);

		QString currentRepoFullName() const;
		QString vaultDir() const;
		QString worktreeDir(const QString &stem) const;
		QString reviewDir(int pr_index) const;
		QString remoteUrlWithCredentials() const;
		QTreeWidgetItem *selectedFileItem() const;
		FileState selectedState() const;
		/// 由 abs_path 反推 rel_path(去掉 workRoot/repo/checkouts|reviews/前綴)
		QString relPathForOpen(const QString &abs_path) const;
		/// 在資料夾樹+清單選到某 rel_path(供 *ByPath 動作定位)
		bool selectFileInUi(const QString &rel_path);

		PdmService *m_service = nullptr;
		PdmGitWorker *m_git = nullptr;

		QLabel *m_account_label = nullptr;
		QGroupBox *m_admin_box = nullptr;      ///< 管理員維護分組(核准者才顯示)
		QPushButton *m_add_button = nullptr;   ///< 新檔入庫
		QComboBox *m_repo_combo = nullptr;
		QTreeWidget *m_folder_tree = nullptr;   ///< 左:資料夾樹
		QTreeWidget *m_tree = nullptr;          ///< 中:所選資料夾的圖檔清單
		QTreeWidget *m_history_tree = nullptr;  ///< 右:所選圖檔的發行歷史
		QString m_current_folder;               ///< 目前選取的資料夾(相對路徑)
		QString m_pending_folder;               ///< 下次重建資料夾樹時要自動選中的資料夾(如新檔入庫後跳到新檔所在夾)
		QStringList m_extra_folders;            ///< 有 .gitkeep 的空資料夾
		QPushButton *m_checkout_button = nullptr,
			    *m_checkin_button = nullptr,
			    *m_cancel_button = nullptr,
			    *m_submit_direct_button = nullptr,  ///< 入庫送審(一步)
			    *m_view_released_button = nullptr,
			    *m_refresh_button = nullptr,
			    *m_submit_button = nullptr,
			    *m_review_button = nullptr,
			    *m_review_button2 = nullptr,  ///< 「核准」組的審核檢視(同功能)
			    *m_approve_button = nullptr,
			    *m_reject_button = nullptr,
			    *m_reject_button2 = nullptr,  ///< 「核准」組的退回(同功能)
			    *m_release_button = nullptr,
			    *m_force_unlock_button = nullptr,
			    *m_revert_button = nullptr,
			    *m_add_folder_button = nullptr,
			    *m_del_folder_button = nullptr,
			    *m_del_drawing_button = nullptr;
		QLabel *m_status_label = nullptr;

		// 專用的出入庫進度對話框(所有 PDM 造成的延遲都用它,不再借用主視窗)
		QDialog *m_busy_dialog = nullptr;
		QLabel *m_busy_label = nullptr;
		QProgressBar *m_busy_bar = nullptr;   ///< 進度框的進度條(漸進式)
		QTimer *m_hide_timer = nullptr;   ///< 進度框收框防抖(佇列空 400ms 後收)
		bool m_op_active = false;         ///< 使用者操作進行中(供 busyGuard)
		int m_op_total = 0;               ///< 本次操作預期 git 步數(0=漸進)
		int m_op_done = 0;                ///< 已完成步數

		QString m_username;
		QString m_user_email;          ///< 登入者 email(比對 work 分支作者)
		bool m_is_confirmer = false;   ///< 在 pdm-confirmers team
		bool m_is_releaser = false;    ///< 在 pdm-releasers team
		bool m_auto_refreshed = false;
		QHash<QString, FileState> m_files;   ///< key = rel_path
};

#endif // PDMDIALOG_H
