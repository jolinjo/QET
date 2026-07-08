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

#include <QDialog>
#include <QHash>
#include <QMap>
#include <QStringList>

#include <functional>

class PdmGitWorker;
class PdmService;
class QComboBox;
class QDomDocument;
class QLabel;
class QProgressBar;
class QPushButton;
class QShowEvent;
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
			QString revision;      ///< 修訂索引(讀自 .qet 的 indexrev)
			QString doc_status;    ///< 文件狀態(讀自 .qet 的 doc-status)
			QString drawn_by;      ///< 繪製者(讀自 .qet 的 author 屬性)
			QString checked_by;    ///< 審核者(讀自 .qet 的 checked-by)
			QString approved_by;   ///< 核准者(讀自 .qet 的 approved-by)
		};

		void setUpWidget();
		void connectionRefreshed();
		void syncRepository();
		void loadFileStates();
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
		/// 小版號進版:「主.次」的次版 +1;純整數 N→N.1;空/舊字母→0.1
		static QString nextMinor(const QString &current);

		void addNewDrawing();   ///< 新增一張圖檔到圖庫(建 work 分支、出庫編輯)
		void checkOut();
		void checkIn();
		void cancelCheckOut();
		void submitForReview();
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
		void loadUserRoles();
		/**
			在 work 分支上簽核:準備 work 分支工作區→戳記(狀態/附加
			欄位)→commit(帶簽核訊息)→push,成功後呼叫 after_push。
			供確認/核准/退回共用(它們都要在 work 分支留 commit)。
		*/
		void signoffOnWorkBranch(const QString &rel_path,
			const QString &status,
			const QMap<QString, QString> &extra_fields,
			const QString &commit_message,
			const std::function<void ()> &after_push);
		void showBusy(bool busy);
		void fail(const QString &title, const QString &log);

		QString currentRepoFullName() const;
		QString vaultDir() const;
		QString worktreeDir(const QString &stem) const;
		QString reviewDir(int pr_index) const;
		QString remoteUrlWithCredentials() const;
		QTreeWidgetItem *selectedFileItem() const;
		FileState selectedState() const;

		PdmService *m_service = nullptr;
		PdmGitWorker *m_git = nullptr;

		QLabel *m_account_label = nullptr;
		QPushButton *m_add_button = nullptr;   ///< 新增圖檔
		QComboBox *m_repo_combo = nullptr;
		QTreeWidget *m_folder_tree = nullptr;   ///< 左:資料夾樹
		QTreeWidget *m_tree = nullptr;          ///< 中:所選資料夾的圖檔清單
		QTreeWidget *m_history_tree = nullptr;  ///< 右:所選圖檔的發行歷史
		QString m_current_folder;               ///< 目前選取的資料夾(相對路徑)
		QStringList m_extra_folders;            ///< 有 .gitkeep 的空資料夾
		QPushButton *m_checkout_button = nullptr,
			    *m_checkin_button = nullptr,
			    *m_cancel_button = nullptr,
			    *m_view_released_button = nullptr,
			    *m_refresh_button = nullptr,
			    *m_submit_button = nullptr,
			    *m_review_button = nullptr,
			    *m_approve_button = nullptr,
			    *m_reject_button = nullptr,
			    *m_release_button = nullptr,
			    *m_force_unlock_button = nullptr,
			    *m_revert_button = nullptr;
		QLabel *m_status_label = nullptr;
		QProgressBar *m_progress = nullptr;

		QString m_username;
		bool m_is_confirmer = false;   ///< 在 pdm-confirmers team
		bool m_is_releaser = false;    ///< 在 pdm-releasers team
		bool m_auto_refreshed = false;
		QHash<QString, FileState> m_files;   ///< key = rel_path
};

#endif // PDMDIALOG_H
