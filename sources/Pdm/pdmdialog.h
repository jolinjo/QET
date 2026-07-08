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

class PdmGitWorker;
class PdmService;
class QComboBox;
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

	public slots:
		void refresh();

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
		};

		void setUpWidget();
		void connectionRefreshed();
		void syncRepository();
		void loadFileStates();
		void loadPullRequests();
		void rebuildTree();
		void updateButtons();

		void checkOut();
		void checkIn();
		void cancelCheckOut();
		void submitForReview();
		void openReviewView();
		void approve();
		void rejectReview();
		void releaseApproved();
		void finishRelease(const QString &rel_path, const QString &stem,
				   const QString &tag, const QString &sha);
		void forceUnlock();
		void showReleaseHistory();

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
		QComboBox *m_repo_combo = nullptr;
		QTreeWidget *m_tree = nullptr;
		QPushButton *m_checkout_button = nullptr,
			    *m_checkin_button = nullptr,
			    *m_cancel_button = nullptr,
			    *m_refresh_button = nullptr,
			    *m_submit_button = nullptr,
			    *m_review_button = nullptr,
			    *m_approve_button = nullptr,
			    *m_reject_button = nullptr,
			    *m_release_button = nullptr,
			    *m_force_unlock_button = nullptr,
			    *m_history_button = nullptr;
		QLabel *m_status_label = nullptr;
		QProgressBar *m_progress = nullptr;

		QString m_username;
		bool m_auto_refreshed = false;
		QHash<QString, FileState> m_files;   ///< key = rel_path
};

#endif // PDMDIALOG_H
