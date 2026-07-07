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
#ifndef PDMDOCKWIDGET_H
#define PDMDOCKWIDGET_H

#include <QDockWidget>
#include <QHash>

class PdmGitWorker;
class PdmService;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

/**
	@brief 「圖檔管理」面板(PDM Phase 1)。
	串接 Gitea:列出 repo 與 .qet 圖檔、顯示鎖定狀態,提供
	出庫(獨佔鎖定)/入庫(推送+解鎖)/取消出庫。
	本機採 vault + worktree 模型,見 doc/pdm-gitea-dev-plan.md §3.4。
*/
class PdmDockWidget : public QDockWidget
{
	Q_OBJECT

	public:
		explicit PdmDockWidget(QWidget *parent = nullptr);

	signals:
		/// 要求編輯器開啟一個 .qet 檔(出庫成功後發出)
		void requestOpenFile(const QString &file_path);

	public slots:
		void refresh();

	private:
		struct FileState {
			QString rel_path;      ///< 相對 vault 的路徑
			QString lock_owner;    ///< 空 = 未鎖定
			bool has_work_branch = false;
		};

		void setUpWidget();
		void connectionRefreshed();
		void syncRepository();
		void loadFileStates();
		void rebuildTree();
		void updateButtons();
		void checkOut();
		void checkIn();
		void cancelCheckOut();
		void showBusy(bool busy);
		void fail(const QString &title, const QString &log);

		QString currentRepoFullName() const;
		QString vaultDir() const;
		QString worktreeDir(const QString &stem) const;
		QString remoteUrlWithCredentials() const;
		QTreeWidgetItem *selectedFileItem() const;

		PdmService *m_service = nullptr;
		PdmGitWorker *m_git = nullptr;

		QLabel *m_account_label = nullptr;
		QComboBox *m_repo_combo = nullptr;
		QTreeWidget *m_tree = nullptr;
		QPushButton *m_checkout_button = nullptr,
			    *m_checkin_button = nullptr,
			    *m_cancel_button = nullptr,
			    *m_refresh_button = nullptr;
		QLabel *m_status_label = nullptr;
		QProgressBar *m_progress = nullptr;

		QString m_username;
		bool m_auto_refreshed = false;
		QHash<QString, FileState> m_files;   ///< key = rel_path
};

#endif // PDMDOCKWIDGET_H
