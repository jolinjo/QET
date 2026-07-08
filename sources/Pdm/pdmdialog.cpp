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
#include "pdmdialog.h"

#include "pdmgitworker.h"
#include "pdmservice.h"
#include "pdmsettings.h"

#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFont>
#include <QDomDocument>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSplitter>
#include <QTextStream>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <memory>

namespace
{
	// Phase 1 固定用 main 當發行線;若公司 repo 範本改名要同步這裡。
	const char *DEFAULT_BRANCH = "main";
	const char *LAST_REPO_KEY  = "pdm/last-repo";

	// 文件狀態(寫入 .qet 圖框 doc-status 附加欄位):由 PDM 流程自動維護。
	// 沿用公司圖框既有雙語值慣例(如「正式發行 Released」)。
	const char *DOC_STATUS_EDITING   = "編輯中 Editing";
	const char *DOC_STATUS_REVIEWING = "審核中 In Review";
	const char *DOC_STATUS_REJECTED  = "退回修改 Rejected";
	const char *DOC_STATUS_RELEASED  = "正式發行 Released";
	const char *STATUS_FIELD_NAME    = "doc-status";

	// 角色 team(Gitea 組織團隊):確認者 / 核准者。
	const char *TEAM_CONFIRMERS = "pdm-confirmers";
	const char *TEAM_RELEASERS  = "pdm-releasers";

	QString sanitizedStem(const QString &rel_path)
	{
		// 圖號即檔名;分支/tag 名不能有空白
		QString stem = QFileInfo(rel_path).completeBaseName();
		stem.replace(' ', '-');
		return stem;
	}

	QString workBranchOf(const QString &rel_path)
	{
		return QStringLiteral("work/") + sanitizedStem(rel_path);
	}

	/**
		lockable 的 OS 唯讀由 git-lfs 在 lock/unlock 時切換,checkout
		當下並不會套用(沙盒實測,見開發計畫附錄 A)。因此凡是「必須
		唯讀」的路徑(審核檢視/發行版檢視)以及「解鎖後的舊工作區」,
		由客戶端自己明確設定檔案權限,不依賴 lfs 的行為。
	*/
	void setFileWritable(const QString &abs_path, bool writable)
	{
		QFile file(abs_path);
		if (!file.exists()) return;
		QFile::Permissions permissions = file.permissions()
			| QFileDevice::ReadOwner | QFileDevice::ReadUser;
		if (writable) {
			permissions |= (QFileDevice::WriteOwner
					| QFileDevice::WriteUser);
		} else {
			permissions &= ~(QFileDevice::WriteOwner
					 | QFileDevice::WriteUser
					 | QFileDevice::WriteGroup
					 | QFileDevice::WriteOther);
		}
		file.setPermissions(permissions);
	}
}

PdmDialog::PdmDialog(QWidget *parent) :
	QDialog(parent),
	m_service(new PdmService(this)),
	m_git(new PdmGitWorker(this))
{
	setObjectName("pdm_dialog");
	setWindowTitle(tr("圖檔管理"));
	resize(1280, 580);
	setUpWidget();

	connect(m_git, &PdmGitWorker::stepStarted, this,
		[this](const QString &cmd) {
			// 不顯示原始 git 指令(對使用者是雜訊),改對應成看得懂的
			// 階段說明。任何 git 步驟一開始就顯示專用進度對話框並更新
			// 說明——不論從工具列或本視窗觸發、有沒有呼叫 showBusy,
			// 都有一致的進度回饋。
			const QString text = friendlyStep(cmd);
			m_status_label->setText(text);
			ensureBusyDialog();
			m_busy_label->setText(text);
			if (!m_busy_dialog->isVisible()) {
				m_busy_dialog->show();
				m_busy_dialog->raise();
			}
		});
	connect(m_git, &PdmGitWorker::allFinished, this,
		[this]() {
			// git 佇列清空 → 收起進度對話框(成功或失敗都會走到)
			if (m_busy_dialog) m_busy_dialog->hide();
			showBusy(false);
		});
}

// 視窗首次顯示才連線,避免程式一啟動就打伺服器
void PdmDialog::showEvent(QShowEvent *event)
{
	QDialog::showEvent(event);
	if (!m_auto_refreshed) {
		m_auto_refreshed = true;
		refresh();
	}
}

void PdmDialog::setUpWidget()
{
	auto *content = new QWidget(this);
	auto *layout = new QVBoxLayout(content);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(4);

	auto *top_row = new QHBoxLayout();
	m_account_label = new QLabel(tr("尚未連線"), content);
	m_add_button = new QPushButton(tr("新增圖檔…"), content);
	m_refresh_button = new QPushButton(tr("重新整理"), content);
	top_row->addWidget(m_account_label, 1);
	top_row->addWidget(m_add_button);
	top_row->addWidget(m_refresh_button);
	layout->addLayout(top_row);

	m_repo_combo = new QComboBox(content);
	layout->addWidget(m_repo_combo);

	// 三欄:資料夾樹 | 圖檔清單 | 所選圖檔的發行歷史
	auto *splitter = new QSplitter(Qt::Horizontal, content);

	m_folder_tree = new QTreeWidget(splitter);
	m_folder_tree->setHeaderLabels({tr("資料夾")});
	m_folder_tree->setSelectionMode(QAbstractItemView::SingleSelection);

	m_tree = new QTreeWidget(splitter);
	m_tree->setHeaderLabels({tr("圖檔"), tr("狀態"), tr("繪製者"),
				 tr("確認者"), tr("核准者"), tr("版本")});
	m_tree->setRootIsDecorated(false);
	m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
	m_tree->setAllColumnsShowFocus(true);
	// 欄寬隨內容自動調整
	m_tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);

	m_history_tree = new QTreeWidget(splitter);
	m_history_tree->setHeaderLabels({tr("版本"), tr("日期時間"),
					 tr("提交者"), tr("訊息")});
	m_history_tree->setRootIsDecorated(true);   // 顯示發行版下的小版縮排
	m_history_tree->setSelectionMode(QAbstractItemView::SingleSelection);
	// 版本/日期/提交者依內容,訊息欄填滿剩餘寬度(避免被切掉)
	m_history_tree->header()->setSectionResizeMode(
		0, QHeaderView::ResizeToContents);
	m_history_tree->header()->setSectionResizeMode(
		1, QHeaderView::ResizeToContents);
	m_history_tree->header()->setSectionResizeMode(
		2, QHeaderView::ResizeToContents);
	m_history_tree->header()->setSectionResizeMode(3, QHeaderView::Stretch);

	splitter->addWidget(m_folder_tree);
	splitter->addWidget(m_tree);
	splitter->addWidget(m_history_tree);
	splitter->setStretchFactor(0, 1);
	splitter->setStretchFactor(1, 3);
	splitter->setStretchFactor(2, 3);
	layout->addWidget(splitter, 1);

	// 三排動作鈕:依選取圖檔的狀態顯示/隱藏
	auto *edit_row = new QHBoxLayout();
	m_checkout_button = new QPushButton(tr("出庫並開啟"), content);
	m_checkin_button = new QPushButton(tr("入庫…"), content);
	m_cancel_button = new QPushButton(tr("取消出庫"), content);
	m_view_released_button = new QPushButton(tr("檢視發行版(唯讀)"), content);
	edit_row->addWidget(m_checkout_button);
	edit_row->addWidget(m_checkin_button);
	edit_row->addWidget(m_cancel_button);
	edit_row->addWidget(m_view_released_button);
	layout->addLayout(edit_row);

	auto *review_row = new QHBoxLayout();
	m_submit_button = new QPushButton(tr("送審…"), content);
	m_review_button = new QPushButton(tr("審核檢視(唯讀)"), content);
	m_approve_button = new QPushButton(tr("確認完畢…"), content);
	m_reject_button = new QPushButton(tr("退回…"), content);
	review_row->addWidget(m_submit_button);
	review_row->addWidget(m_review_button);
	review_row->addWidget(m_approve_button);
	review_row->addWidget(m_reject_button);
	layout->addLayout(review_row);

	// 發行歷史改為右側面板自動顯示(選檔即載入),不再需要按鈕
	auto *release_row = new QHBoxLayout();
	m_release_button = new QPushButton(tr("核准發行…"), content);
	m_revert_button = new QPushButton(tr("退回上一發行版…"), content);
	m_force_unlock_button = new QPushButton(tr("強制解鎖…"), content);
	release_row->addWidget(m_release_button);
	release_row->addWidget(m_revert_button);
	release_row->addWidget(m_force_unlock_button);
	layout->addLayout(release_row);

	// 結構性維運按鈕列:僅核准者可見
	auto *admin_row = new QHBoxLayout();
	m_add_folder_button = new QPushButton(tr("新增資料夾…"), content);
	m_del_folder_button = new QPushButton(tr("刪除資料夾…"), content);
	m_del_drawing_button = new QPushButton(tr("刪除圖檔…"), content);
	admin_row->addWidget(m_add_folder_button);
	admin_row->addWidget(m_del_folder_button);
	admin_row->addWidget(m_del_drawing_button);
	layout->addLayout(admin_row);

	// 進度顯示沿用元件庫「更新公司庫」模式:按鈕下方內嵌,不跳對話框
	m_status_label = new QLabel(content);
	m_status_label->setWordWrap(true);
	layout->addWidget(m_status_label);
	m_progress = new QProgressBar(content);
	m_progress->setTextVisible(false);
	m_progress->hide();
	layout->addWidget(m_progress);

	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->addWidget(content);

	connect(m_refresh_button, &QPushButton::clicked,
		this, &PdmDialog::refresh);
	connect(m_add_button, &QPushButton::clicked,
		this, &PdmDialog::addNewDrawing);
	connect(m_repo_combo, &QComboBox::currentIndexChanged, this,
		[this](int index) {
			if (index < 0) return;
			QSettings().setValue(LAST_REPO_KEY, currentRepoFullName());
			syncRepository();
		});
	connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this]() {
		updateButtons();
		// 選檔即在右側面板載入該檔發行歷史
		const QTreeWidgetItem *item = selectedFileItem();
		m_history_tree->clear();
		if (item)
			loadReleaseHistory(item->data(0, Qt::UserRole).toString());
	});
	// 雙擊發行歷史某版本→唯讀開啟
	connect(m_history_tree, &QTreeWidget::itemDoubleClicked, this,
		[this](QTreeWidgetItem *hist_item) {
			const QTreeWidgetItem *file_item = selectedFileItem();
			if (!file_item || !hist_item) return;
			openReleaseRevision(hist_item->text(0),
				file_item->data(0, Qt::UserRole).toString());
		});
	connect(m_folder_tree, &QTreeWidget::itemSelectionChanged, this,
		[this]() {
			QTreeWidgetItem *item = m_folder_tree->currentItem();
			m_current_folder = item ? item->text(0) : QString();
			populateFileList();
			updateButtons();
		});

	// 結構性維運右鍵選單:限核准者(pdm-releasers)才顯示
	m_folder_tree->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_folder_tree, &QTreeWidget::customContextMenuRequested, this,
		[this](const QPoint &pos) {
			if (!m_is_releaser) return;
			QTreeWidgetItem *it = m_folder_tree->itemAt(pos);
			if (it) m_folder_tree->setCurrentItem(it);
			QMenu menu(this);
			menu.addAction(tr("新增資料夾…"), this, &PdmDialog::addFolder);
			if (it && it->text(0) != tr("(根目錄)"))
				menu.addAction(tr("刪除此資料夾…"),
					this, &PdmDialog::deleteFolder);
			menu.exec(m_folder_tree->viewport()->mapToGlobal(pos));
		});
	m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_tree, &QTreeWidget::customContextMenuRequested, this,
		[this](const QPoint &pos) {
			if (!m_is_releaser) return;
			QTreeWidgetItem *it = m_tree->itemAt(pos);
			if (!it) return;
			m_tree->setCurrentItem(it);
			QMenu menu(this);
			menu.addAction(tr("刪除此圖檔…"),
				this, &PdmDialog::deleteDrawing);
			menu.exec(m_tree->viewport()->mapToGlobal(pos));
		});
	connect(m_checkout_button, &QPushButton::clicked,
		this, &PdmDialog::checkOut);
	connect(m_checkin_button, &QPushButton::clicked,
		this, &PdmDialog::checkIn);
	connect(m_cancel_button, &QPushButton::clicked,
		this, &PdmDialog::cancelCheckOut);
	connect(m_view_released_button, &QPushButton::clicked,
		this, &PdmDialog::viewReleased);
	connect(m_submit_button, &QPushButton::clicked,
		this, &PdmDialog::submitForReview);
	connect(m_review_button, &QPushButton::clicked,
		this, &PdmDialog::openReviewView);
	connect(m_approve_button, &QPushButton::clicked,
		this, &PdmDialog::confirmDone);
	connect(m_reject_button, &QPushButton::clicked,
		this, &PdmDialog::rejectReview);
	connect(m_release_button, &QPushButton::clicked,
		this, &PdmDialog::approveAndRelease);
	connect(m_force_unlock_button, &QPushButton::clicked,
		this, &PdmDialog::forceUnlock);
	connect(m_revert_button, &QPushButton::clicked,
		this, &PdmDialog::revertToRelease);
	connect(m_add_folder_button, &QPushButton::clicked,
		this, &PdmDialog::addFolder);
	connect(m_del_folder_button, &QPushButton::clicked,
		this, &PdmDialog::deleteFolder);
	connect(m_del_drawing_button, &QPushButton::clicked,
		this, &PdmDialog::deleteDrawing);

	updateButtons();
}

void PdmDialog::startBackgroundConnect()
{
	// 只在尚未連過時背景連一次(showEvent 首次顯示的連線由此取代)
	if (m_auto_refreshed) return;
	m_auto_refreshed = true;
	refresh();
}

void PdmDialog::refresh()
{
	if (PdmSettings::token().isEmpty()) {
		emit connectionReady(false);
		m_account_label->setText(
			tr("尚未設定:請至偏好設定→圖檔管理填入伺服器與 token"));
		m_repo_combo->clear();
		m_folder_tree->clear();
		m_tree->clear();
		m_files.clear();
		updateButtons();
		return;
	}
	m_account_label->setText(tr("連線中…"));
	m_service->verifyConnection([this](bool ok, const QString &login_or_error) {
		if (!ok) {
			emit connectionReady(false);
			m_account_label->setText(tr("連線失敗:%1").arg(login_or_error));
			return;
		}
		m_username = login_or_error;
		PdmSettings::setUsername(m_username);
		m_account_label->setText(tr("帳號:%1").arg(m_username));
		// 取 email 供比對 work 分支作者(判斷「繪製者本人」)
		m_service->get(QStringLiteral("/user"),
			[this](const PdmService::Reply &reply) {
				if (reply.ok)
					m_user_email = reply.json.object()
						.value(QStringLiteral("email")).toString();
			});
		loadUserRoles();
		connectionRefreshed();
	});
}

void PdmDialog::loadUserRoles()
{
	m_is_confirmer = false;
	m_is_releaser = false;
	// 本 repo 所屬組織(owner)底下的 team 才算數
	const QString org = currentRepoFullName().section('/', 0, 0);
	m_service->get(QStringLiteral("/user/teams?limit=50"),
		[this, org](const PdmService::Reply &reply) {
			if (!reply.ok) { updateButtons(); return; }
			const QJsonArray teams = reply.json.array();
			for (const QJsonValue &value : teams) {
				const QJsonObject team = value.toObject();
				const QString team_org = team.value(
					QStringLiteral("organization")).toObject()
					.value(QStringLiteral("username")).toString();
				if (team_org != org) continue;
				const QString name = team.value(
					QStringLiteral("name")).toString();
				if (name == QLatin1String(TEAM_CONFIRMERS))
					m_is_confirmer = true;
				else if (name == QLatin1String(TEAM_RELEASERS))
					m_is_releaser = true;
			}
			updateButtons();
		});
}

void PdmDialog::signoffOnWorkBranch(const QString &rel_path,
	const QString &status, const QMap<QString, QString> &extra_fields,
	const QString &commit_message,
	const std::function<void ()> &after_push)
{
	const QString stem = sanitizedStem(rel_path);
	const QString branch = workBranchOf(rel_path);
	const QString worktree = worktreeDir(stem);
	const QString vault = vaultDir();
	const QString abs_path = worktree + '/' + rel_path;

	// 戳記→add→commit→push→after_push(工作區已在 origin/branch 最新狀態)
	auto stamp_and_push = [this, rel_path, branch, worktree, abs_path,
			       status, extra_fields, commit_message, after_push]
		(const PdmGitWorker::Result &prep) {
		if (!prep.ok) {
			fail(tr("準備簽核工作區失敗"), prep.output);
			return;
		}
		setFileWritable(abs_path, true);
		if (!stampDocFields(abs_path, status, QString(), false,
				    extra_fields)) {
			fail(tr("寫入圖框欄位失敗"), abs_path);
			return;
		}
		m_git->enqueue({"add", "--", rel_path}, worktree, {});
		m_git->enqueue({"commit", "-m", commit_message}, worktree, {});
		m_git->enqueue({"push", "origin", branch}, worktree,
			[this, after_push](const PdmGitWorker::Result &push_result) {
				if (!push_result.ok) {
					fail(tr("推送簽核 commit 失敗"),
					     push_result.output);
					return;
				}
				after_push();
			});
	};

	showBusy(true);
	m_git->enqueue({"fetch", "origin", "--prune"}, vault, {});
	if (QDir(worktree).exists()) {
		m_git->enqueue({"checkout", branch}, worktree, {});
		m_git->enqueue({"reset", "--hard",
			QStringLiteral("origin/") + branch}, worktree,
			stamp_and_push);
	} else {
		m_git->enqueue({"worktree", "add", "--track", "-b", branch,
			worktree, QStringLiteral("origin/") + branch},
			vault, stamp_and_push);
	}
}

void PdmDialog::connectionRefreshed()
{
	// 固定圖庫模式:偏好設定指定了 repo,直接連該 repo,不列出其他也不給選。
	const QString fixed_repo = PdmSettings::repo();
	if (!fixed_repo.isEmpty()) {
		const QSignalBlocker blocker(m_repo_combo);
		m_repo_combo->clear();
		m_repo_combo->addItem(fixed_repo);
		m_repo_combo->setCurrentIndex(0);
		m_repo_combo->hide();
		syncRepository();
		return;
	}

	m_repo_combo->show();
	m_service->listRepositories([this](const PdmService::Reply &reply) {
		if (!reply.ok) {
			m_account_label->setText(reply.error);
			return;
		}
		const QString last = QSettings().value(LAST_REPO_KEY).toString();
		const QSignalBlocker blocker(m_repo_combo);
		m_repo_combo->clear();
		const QJsonArray repos = reply.json.object()
			.value(QStringLiteral("data")).toArray();
		for (const QJsonValue &value : repos) {
			const QString full_name = value.toObject()
				.value(QStringLiteral("full_name")).toString();
			if (!full_name.isEmpty()) m_repo_combo->addItem(full_name);
		}
		const int last_index = m_repo_combo->findText(last);
		m_repo_combo->setCurrentIndex(last_index >= 0 ? last_index : 0);
		syncRepository();
	});
}

void PdmDialog::syncRepository()
{
	const QString repo = currentRepoFullName();
	if (repo.isEmpty()) return;
	showBusy(true);

	const QString vault = vaultDir();
	if (QDir(vault + QStringLiteral("/.git")).exists()) {
		m_git->enqueue({"fetch", "origin", "--prune"}, vault, {});
		m_git->enqueue({"checkout", DEFAULT_BRANCH}, vault, {});
		m_git->enqueue({"pull", "--ff-only"}, vault,
			[this](const PdmGitWorker::Result &result) {
				if (!result.ok) {
					fail(tr("更新圖庫失敗"), result.output);
					return;
				}
				loadFileStates();
			});
	} else {
		QDir().mkpath(QFileInfo(vault).absolutePath());
		m_git->enqueue({"clone", remoteUrlWithCredentials(), vault}, {},
			[this, vault](const PdmGitWorker::Result &result) {
				if (!result.ok) {
					fail(tr("下載圖庫失敗"), result.output);
					return;
				}
				// lockable 唯讀與 pre-push 鎖驗證需要 lfs hooks
				m_git->enqueue({"lfs", "install", "--local"}, vault, {});
				loadFileStates();
			});
	}
}

void PdmDialog::loadFileStates()
{
	const QString vault = vaultDir();
	m_files.clear();
	m_extra_folders.clear();

	// 以 .gitkeep 佔位的空資料夾(新增資料夾後尚無 .qet 時仍要顯示)
	m_git->enqueue({"ls-files", "--", "*.gitkeep"}, vault,
		[this](const PdmGitWorker::Result &result) {
			const QStringList lines = result.output.split('\n',
				Qt::SkipEmptyParts);
			for (const QString &line : lines) {
				const QString dir = QFileInfo(line.trimmed()).path();
				if (dir != QLatin1String(".")
				    && !m_extra_folders.contains(dir))
					m_extra_folders << dir;
			}
		});

	m_git->enqueue({"ls-files", "--", "*.qet"}, vault,
		[this, vault](const PdmGitWorker::Result &result) {
			const QStringList lines = result.output.split('\n',
				Qt::SkipEmptyParts);
			for (const QString &line : lines) {
				FileState state;
				state.rel_path = line.trimmed();
				// 版本/狀態/繪製者/審核者/核准者讀自 vault 的 .qet 首頁圖框
				readDocFields(vault + '/' + state.rel_path, &state);
				m_files.insert(state.rel_path, state);
			}
		});

	// work 分支末次 commit 作者(判斷「繪製者本人」用,尤其入庫未送審時
	// 已解鎖、無 PR,靠此認人)
	m_git->enqueue({"for-each-ref",
		QStringLiteral("--format=%(refname:short)%x1f%(authoremail)"),
		QStringLiteral("refs/remotes/origin/work/")}, vault,
		[this](const PdmGitWorker::Result &result) {
			const QStringList lines = result.output.split('\n',
				Qt::SkipEmptyParts);
			QHash<QString, QString> branch_author;
			for (const QString &line : lines) {
				const QStringList f = line.split(QChar(0x1f));
				if (f.size() < 2) continue;
				QString ref = f.at(0);   // origin/work/<stem>
				QString email = f.at(1);
				email.remove(QLatin1Char('<')).remove(QLatin1Char('>'));
				if (ref.startsWith(QLatin1String("origin/")))
					branch_author.insert(ref.mid(7), email.trimmed());
			}
			for (auto it = m_files.begin(); it != m_files.end(); ++it)
				it->work_author = branch_author.value(
					workBranchOf(it.key()));
		});

	m_git->enqueue({"lfs", "locks", "--json"}, vault,
		[this](const PdmGitWorker::Result &result) {
			const QJsonArray locks = QJsonDocument::fromJson(
				result.output.toUtf8()).array();
			for (const QJsonValue &value : locks) {
				const QJsonObject lock = value.toObject();
				const QString path = lock.value(
					QStringLiteral("path")).toString();
				auto iterator = m_files.find(path);
				if (iterator == m_files.end()) continue;
				iterator->lock_owner = lock.value(QStringLiteral("owner"))
					.toObject().value(QStringLiteral("name")).toString();
			}
		});

	m_git->enqueue({"ls-remote", "--heads", "origin", "refs/heads/work/*"},
		vault,
		[this](const PdmGitWorker::Result &result) {
			QSet<QString> branches;
			const QStringList lines = result.output.split('\n',
				Qt::SkipEmptyParts);
			for (const QString &line : lines) {
				const int position = line.indexOf(
					QStringLiteral("refs/heads/"));
				if (position >= 0)
					branches.insert(line.mid(position + 11).trimmed());
			}
			for (auto iterator = m_files.begin();
			     iterator != m_files.end(); ++iterator) {
				iterator->has_work_branch = branches.contains(
					workBranchOf(iterator->rel_path));
			}

			// 進行中的圖檔:版本/狀態/簽核者改讀 work 分支的圖框
			//(這些值尚未合併回 main,清單直接讀 main 會顯示成舊值)
			QStringList work_paths;
			for (auto it = m_files.constBegin();
			     it != m_files.constEnd(); ++it)
				if (it->has_work_branch) work_paths << it.key();
			if (work_paths.isEmpty()) {
				loadPullRequests();
				return;
			}
			auto remaining = std::make_shared<int>(work_paths.size());
			for (const QString &rp : work_paths) {
				m_git->enqueue({"show", QStringLiteral("origin/")
					+ workBranchOf(rp) + ':' + rp}, vaultDir(),
					[this, rp, remaining]
					(const PdmGitWorker::Result &show_r) {
					QDomDocument doc;
					if (doc.setContent(show_r.output)) {
						auto it = m_files.find(rp);
						if (it != m_files.end())
							parseDocFields(doc, &(*it));
					}
					if (--(*remaining) == 0) loadPullRequests();
				});
			}
		});
}

void PdmDialog::loadPullRequests()
{
	m_service->listOpenPullRequests(currentRepoFullName(),
		[this](const PdmService::Reply &reply) {
			if (!reply.ok) {
				fail(tr("讀取送審清單失敗"), reply.error);
				rebuildTree();
				return;
			}
			// head 分支 work/<圖號> → 對回圖檔
			QHash<QString, QString> branch_to_path;
			for (auto iterator = m_files.constBegin();
			     iterator != m_files.constEnd(); ++iterator) {
				branch_to_path.insert(
					workBranchOf(iterator.key()), iterator.key());
			}
			QList<int> pending_reviews;
			const QJsonArray pulls = reply.json.array();
			for (const QJsonValue &value : pulls) {
				const QJsonObject pull = value.toObject();
				const QJsonObject head = pull.value(
					QStringLiteral("head")).toObject();
				const QString branch = head.value(
					QStringLiteral("ref")).toString();
				const QString path = branch_to_path.value(branch);
				if (path.isEmpty()) continue;
				FileState &state = m_files[path];
				state.pr_index = pull.value(
					QStringLiteral("number")).toInt();
				state.pr_author = pull.value(QStringLiteral("user"))
					.toObject().value(QStringLiteral("login"))
					.toString();
				state.pr_head_sha = head.value(
					QStringLiteral("sha")).toString();
				pending_reviews << state.pr_index;
			}
			if (pending_reviews.isEmpty()) {
				rebuildTree();
				return;
			}
			// 逐 PR 查核准狀態(開啟中 PR 數量少,逐一查可接受)
			auto remaining = std::make_shared<int>(
				pending_reviews.size());
			for (int pr_index : pending_reviews) {
				m_service->get(QStringLiteral(
					"/repos/%1/pulls/%2/reviews")
					.arg(currentRepoFullName()).arg(pr_index),
					[this, pr_index, remaining]
					(const PdmService::Reply &review_reply) {
					bool approved = false;
					const QJsonArray reviews =
						review_reply.json.array();
					for (const QJsonValue &value : reviews) {
						const QJsonObject review =
							value.toObject();
						if (review.value(QStringLiteral(
							"state")).toString()
						    == QLatin1String("APPROVED")
						    && !review.value(QStringLiteral(
							"dismissed")).toBool()) {
							approved = true;
						}
					}
					for (auto iterator = m_files.begin();
					     iterator != m_files.end();
					     ++iterator) {
						if (iterator->pr_index == pr_index)
							iterator->pr_approved
								= approved;
					}
					if (--(*remaining) == 0) rebuildTree();
				});
			}
		});
}

void PdmDialog::readDocFields(const QString &abs_path, FileState *state) const
{
	if (!state) return;
	QFile file(abs_path);
	if (!file.open(QIODevice::ReadOnly)) return;
	QDomDocument doc;
	if (!doc.setContent(&file)) { file.close(); return; }
	file.close();
	parseDocFields(doc, state);
}

void PdmDialog::parseDocFields(const QDomDocument &doc, FileState *state) const
{
	if (!state) return;
	// 取首頁(第一個 <diagram>)的圖框欄位為整檔代表值
	const QDomElement diagram =
		doc.documentElement().firstChildElement(QStringLiteral("diagram"));
	if (diagram.isNull()) return;

	state->revision = diagram.attribute(QStringLiteral("indexrev"));
	state->drawn_by = diagram.attribute(QStringLiteral("author"));

	// 附加欄位:文件狀態、審核者、核准者
	const QDomElement properties = diagram.firstChildElement(
		QStringLiteral("properties"));
	for (QDomElement p = properties.firstChildElement(
		QStringLiteral("property"));
	     !p.isNull();
	     p = p.nextSiblingElement(QStringLiteral("property"))) {
		const QString name = p.attribute(QStringLiteral("name"));
		if (name == QLatin1String(STATUS_FIELD_NAME))
			state->doc_status = p.text();
		else if (name == QLatin1String("checked-by"))
			state->checked_by = p.text();
		else if (name == QLatin1String("approved-by"))
			state->approved_by = p.text();
	}
}

bool PdmDialog::stampDocFields(const QString &abs_path, const QString &status,
			      const QString &revision, bool set_revision,
			      const QMap<QString, QString> &extra_fields) const
{
	QFile file(abs_path);
	if (!file.open(QIODevice::ReadOnly)) return false;
	QDomDocument doc;
	if (!doc.setContent(&file)) { file.close(); return false; }
	file.close();

	// 要寫入的附加欄位:文件狀態(若有)＋額外欄位
	QMap<QString, QString> props = extra_fields;
	if (!status.isEmpty())
		props.insert(QLatin1String(STATUS_FIELD_NAME), status);

	// 在單一 <diagram> 內找/建名為 name 的 <property> 並設定文字
	auto setProperty = [&doc](QDomElement &diagram, const QString &name,
				  const QString &value) {
		QDomElement properties = diagram.firstChildElement(
			QStringLiteral("properties"));
		if (properties.isNull()) {
			properties = doc.createElement(
				QStringLiteral("properties"));
			diagram.appendChild(properties);
		}
		QDomElement target;
		for (QDomElement p = properties.firstChildElement(
			QStringLiteral("property"));
		     !p.isNull();
		     p = p.nextSiblingElement(QStringLiteral("property"))) {
			if (p.attribute(QStringLiteral("name")) == name) {
				target = p;
				break;
			}
		}
		if (target.isNull()) {
			target = doc.createElement(QStringLiteral("property"));
			target.setAttribute(QStringLiteral("name"), name);
			target.setAttribute(QStringLiteral("show"), "1");
			properties.appendChild(target);
		}
		while (target.hasChildNodes())
			target.removeChild(target.firstChild());
		target.appendChild(doc.createTextNode(value));
	};

	QDomElement root = doc.documentElement();
	bool changed = false;
	for (QDomElement diagram = root.firstChildElement(
		QStringLiteral("diagram"));
	     !diagram.isNull();
	     diagram = diagram.nextSiblingElement(QStringLiteral("diagram"))) {
		if (set_revision) {
			// revision 可為空字串以清空版本(編輯中不顯示版本)
			diagram.setAttribute(QStringLiteral("indexrev"), revision);
			changed = true;
		}
		for (auto it = props.constBegin(); it != props.constEnd(); ++it) {
			setProperty(diagram, it.key(), it.value());
			changed = true;
		}
	}
	if (!changed) return true;

	QFile out(abs_path);
	if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
	QTextStream stream(&out);
	stream << doc.toString(2);
	out.close();
	return true;
}

QString PdmDialog::nextMinor(const QString &current)
{
	// 「主.次」→ 次版 +1;純整數 N(已發行版)→ N.1;空/舊字母 → 0.1
	const QStringList parts = current.trimmed().split(QLatin1Char('.'));
	if (parts.size() >= 2) {
		bool maj_ok = false, min_ok = false;
		const int maj = parts.at(0).toInt(&maj_ok);
		const int min = parts.at(1).toInt(&min_ok);
		if (maj_ok && min_ok)
			return QStringLiteral("%1.%2").arg(maj).arg(min + 1);
	}
	bool int_ok = false;
	const int n = current.trimmed().toInt(&int_ok);
	return int_ok ? QStringLiteral("%1.1").arg(n) : QStringLiteral("0.1");
}

void PdmDialog::rebuildTree()
{
	rebuildFolderTree();
	populateFileList();
	m_status_label->setText(tr("共 %1 個圖檔").arg(m_files.size()));
	updateButtons();
	emit connectionReady(true);   // 已連上並取回資料
}

void PdmDialog::rebuildFolderTree()
{
	const QString previous = m_current_folder;
	const QSignalBlocker blocker(m_folder_tree);
	m_folder_tree->clear();

	// 從所有圖檔路徑收集資料夾(目前單層,結構已可容納多層)
	QStringList folders;
	for (const QString &path : m_files.keys()) {
		const QString dir = QFileInfo(path).path();   // 無資料夾時為 "."
		const QString folder = (dir == QLatin1String("."))
			? tr("(根目錄)") : dir;
		if (!folders.contains(folder)) folders << folder;
	}
	// 加入以 .gitkeep 佔位、尚無圖檔的空資料夾
	for (const QString &dir : m_extra_folders)
		if (!folders.contains(dir)) folders << dir;
	folders.sort();

	QTreeWidgetItem *to_select = nullptr;
	for (const QString &folder : folders) {
		auto *item = new QTreeWidgetItem(m_folder_tree, {folder});
		if (folder == previous) to_select = item;
	}
	if (!to_select && m_folder_tree->topLevelItemCount() > 0)
		to_select = m_folder_tree->topLevelItem(0);
	if (to_select) {
		m_folder_tree->setCurrentItem(to_select);
		m_current_folder = to_select->text(0);
	} else {
		m_current_folder.clear();
	}
}

void PdmDialog::populateFileList()
{
	const QString selected = m_tree->currentItem()
		? m_tree->currentItem()->data(0, Qt::UserRole).toString()
		: QString();
	m_tree->clear();

	// 依 git/LFS/PR 狀態推導的即時流程狀態(驅動按鈕、也決定顯示)
	const auto lifecycleStatus = [this](const FileState &state) -> QString {
		const QString owner = state.lock_owner;
		if (state.pr_index > 0)
			return state.pr_approved
				? tr("已確認待發行(#%1)").arg(state.pr_index)
				: tr("審核中(#%1)").arg(state.pr_index);
		if (owner == m_username && !owner.isEmpty())
			return tr("編輯中(我)");
		if (!owner.isEmpty()) return tr("出庫中");
		if (state.has_work_branch) return tr("已入庫未送審");
		return tr("可出庫");
	};

	QStringList paths = m_files.keys();
	paths.sort();
	for (const QString &path : paths) {
		const QString dir = QFileInfo(path).path();
		const QString folder = (dir == QLatin1String("."))
			? tr("(根目錄)") : dir;
		if (folder != m_current_folder) continue;

		const FileState &state = m_files.value(path);
		// 進行中的圖檔(已出庫/送審/入庫未發行)顯示即時流程狀態;
		// 閒置(已發行)才顯示檔內文件狀態。清單讀 main,故進行中的
		// 檔內狀態尚未合併回 main,以即時狀態呈現才不會顯示成舊值。
		const bool active = !state.lock_owner.isEmpty()
			|| state.pr_index > 0 || state.has_work_branch;
		const QString status = active
			? lifecycleStatus(state)
			: (state.doc_status.isEmpty()
				? lifecycleStatus(state) : state.doc_status);
		// 右側只顯示檔名;完整相對路徑存在 UserRole 供動作用
		auto *item = new QTreeWidgetItem(m_tree,
			{QFileInfo(path).fileName(),
			 status,
			 state.drawn_by,
			 state.checked_by,
			 state.approved_by,
			 state.revision});
		item->setData(0, Qt::UserRole, path);
		if (path == selected) m_tree->setCurrentItem(item);
	}
}

void PdmDialog::updateButtons()
{
	const QTreeWidgetItem *item = selectedFileItem();
	const FileState state = item ? m_files.value(item->data(0, Qt::UserRole).toString()) : FileState();
	const bool has_selection = (item != nullptr);
	const bool locked_by_me = has_selection && !m_username.isEmpty()
				  && state.lock_owner == m_username;
	const bool locked_by_other = has_selection && !state.lock_owner.isEmpty()
				     && !locked_by_me;
	const bool in_review = has_selection && state.pr_index > 0;

	// 出庫:未鎖定→出庫並開啟;已被自己出庫→繼續編輯(重開工作區);
	// 送審中/他人鎖定則不可。
	m_checkout_button->setEnabled(has_selection && !in_review
		&& (state.lock_owner.isEmpty() || locked_by_me));
	m_checkout_button->setText(locked_by_me ? tr("繼續編輯")
						: tr("出庫並開啟"));
	m_checkin_button->setEnabled(locked_by_me && !in_review);
	m_cancel_button->setEnabled(locked_by_me);
	// 檢視發行版(唯讀):清單內的圖檔都在 main 上,隨時可看發行版
	m_view_released_button->setEnabled(has_selection);

	// 送審:已入庫(work 分支存在)、未鎖定、尚無 PR
	m_submit_button->setEnabled(has_selection && state.has_work_branch
				    && state.lock_owner.isEmpty() && !in_review);
	// 審核檢視:有 PR 即可(唯讀)
	m_review_button->setEnabled(in_review);
	// 開發期暫放寬「非製圖者本人」限制,方便單人自測整套流程;
	// 正式版應恢復 (state.pr_author != m_username) 條件。
	m_approve_button->setEnabled(in_review && m_is_confirmer);
	m_reject_button->setEnabled(in_review && (m_is_confirmer || m_is_releaser));
	m_release_button->setEnabled(in_review && m_is_releaser);
	m_force_unlock_button->setVisible(locked_by_other);
	m_force_unlock_button->setEnabled(locked_by_other);
	// 退回上一發行版:有進行中的 work 分支才有得退;限製圖者本人或核准者。
	// 「本人」以鎖定者/送審者/work 分支作者 email 任一相符判定(入庫未送審
	// 時已解鎖、無 PR,靠 work 分支作者認人)。
	const bool mine =
		(state.lock_owner == m_username && !m_username.isEmpty())
		|| (state.pr_author == m_username && !m_username.isEmpty())
		|| (!state.work_author.isEmpty()
		    && state.work_author == m_user_email);
	m_revert_button->setEnabled(has_selection && state.has_work_branch
				    && (mine || m_is_releaser));

	// 結構性維運:僅核准者可見;刪除需選到對應項目、且該圖檔閒置
	const bool idle = has_selection && state.lock_owner.isEmpty()
			  && !state.has_work_branch && state.pr_index == 0;
	const bool folder_selected = m_folder_tree->currentItem()
		&& m_folder_tree->currentItem()->text(0) != tr("(根目錄)");
	m_add_folder_button->setVisible(m_is_releaser);
	m_del_folder_button->setVisible(m_is_releaser);
	m_del_drawing_button->setVisible(m_is_releaser);
	m_del_folder_button->setEnabled(folder_selected);
	m_del_drawing_button->setEnabled(idle);
}

void PdmDialog::addNewDrawing()
{
	// 取目前開啟的圖檔:請編輯器存檔後以路徑回呼 addDrawingFromFile
	emit requestAddCurrentDrawing();
}

void PdmDialog::addDrawingFromFile(const QString &source)
{
	if (currentRepoFullName().isEmpty() || source.isEmpty()) return;

	// 選專案資料夾 + 圖號
	QDialog dlg(this);
	dlg.setWindowTitle(tr("新增圖檔到圖庫"));
	auto *form = new QFormLayout(&dlg);
	auto *folder_cb = new QComboBox(&dlg);
	folder_cb->setEditable(true);
	QStringList folders;
	for (const QString &p : m_files.keys()) {
		const QString d = QFileInfo(p).path();
		if (d != QLatin1String(".") && !folders.contains(d)) folders << d;
	}
	folders.sort();
	folder_cb->addItems(folders);
	auto *num_le = new QLineEdit(QFileInfo(source).completeBaseName(), &dlg);
	form->addRow(tr("專案資料夾:"), folder_cb);
	form->addRow(tr("圖號(檔名):"), num_le);
	auto *hint = new QLabel(tr("圖號需全庫唯一、勿含空白。"), &dlg);
	hint->setWordWrap(true);
	form->addRow(hint);
	auto *buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	if (dlg.exec() != QDialog::Accepted) return;

	const QString folder = folder_cb->currentText().trimmed();
	QString number = num_le->text().trimmed();
	number.replace(QLatin1Char(' '), QLatin1Char('-'));
	if (number.isEmpty()) {
		QMessageBox::warning(this, tr("新增圖檔"), tr("圖號不可空白。"));
		return;
	}
	const QString rel_path = (folder.isEmpty() ? number
					: folder + QLatin1Char('/') + number)
				 + QStringLiteral(".qet");
	const QString stem = sanitizedStem(rel_path);
	// 圖號(stem)全庫唯一:分支/tag 以 stem 為鍵,不同資料夾也不可同名
	for (const QString &p : m_files.keys()) {
		if (p == rel_path || sanitizedStem(p) == stem) {
			QMessageBox::warning(this, tr("新增圖檔"),
				tr("圖號「%1」已存在於圖庫,請換一個。").arg(stem));
			return;
		}
	}

	const QString branch = workBranchOf(rel_path);
	const QString worktree = worktreeDir(stem);
	const QString vault = vaultDir();
	const QString abs_path = worktree + '/' + rel_path;

	showBusy(true);
	m_git->enqueue({"fetch", "origin", "--prune"}, vault, {});
	// 從 main 開一個新 work 分支的工作區,把來源圖檔放進去
	m_git->enqueue({"worktree", "add", "-b", branch, worktree,
		QStringLiteral("origin/") + DEFAULT_BRANCH}, vault,
		[this, source, rel_path, abs_path, branch, worktree, vault]
		(const PdmGitWorker::Result &r) {
		if (!r.ok) { fail(tr("建立工作區失敗"), r.output); return; }

		QDir().mkpath(QFileInfo(abs_path).absolutePath());
		QFile::remove(abs_path);
		if (!QFile::copy(source, abs_path)) {
			fail(tr("複製圖檔失敗"), abs_path);
			return;
		}
		setFileWritable(abs_path, true);
		// 初版 0.1、編輯中、清空確認/核准欄位
		stampDocFields(abs_path, QString::fromUtf8(DOC_STATUS_EDITING),
			nextMinor(QString()), true,
			{{QStringLiteral("checked-by"), QString()},
			 {QStringLiteral("approved-by"), QString()}});

		m_git->enqueue({"add", "--", rel_path}, worktree, {});
		m_git->enqueue({"commit", "-m",
			tr("新增圖檔：%1").arg(rel_path)}, worktree, {});
		m_git->enqueue({"push", "-u", "origin", branch}, worktree,
			[this, rel_path, abs_path, worktree]
			(const PdmGitWorker::Result &push_r) {
			if (!push_r.ok) {
				fail(tr("推送失敗"), push_r.output);
				return;
			}
			// 上鎖(從工作區,該檔在此分支存在)
			m_git->enqueue({"lfs", "lock", rel_path}, worktree,
				[this, abs_path](const PdmGitWorker::Result &lock_r) {
				if (!lock_r.ok)
					fail(tr("上鎖失敗(仍可編輯,請稍後於清單重試)"),
					     lock_r.output);
				emit requestOpenFile(abs_path);
				refresh();
			});
		});
	});
}

void PdmDialog::checkOut()
{
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->data(0, Qt::UserRole).toString();
	const FileState state = m_files.value(rel_path);
	const QString vault = vaultDir();
	const QString branch = workBranchOf(rel_path);
	const QString worktree = worktreeDir(sanitizedStem(rel_path));

	// 已被自己出庫(上次沒入庫就關閉):直接重開工作區的檔繼續編輯,
	// 不重新鎖定、不進版、不重置修改。
	if (state.lock_owner == m_username && !m_username.isEmpty()) {
		const QString abs_path = worktree + '/' + rel_path;
		if (QFile::exists(abs_path)) {
			setFileWritable(abs_path, true);
			emit requestOpenFile(abs_path);
			return;
		}
		// 工作區不在了(換機器)→ 走下面的正常流程重建工作區
	}

	showBusy(true);
	// 鎖定成功才有編輯權;任何後續失敗都不影響「鎖是我的」這個事實
	m_git->enqueue({"lfs", "lock", rel_path}, vault,
		[this, rel_path, state, vault, branch, worktree]
		(const PdmGitWorker::Result &result) {
			if (!result.ok) {
				fail(tr("出庫失敗(可能剛被他人鎖定)"), result.output);
				refresh();
				return;
			}
			const QString abs_path = worktree + '/' + rel_path;
			auto open_and_refresh = [this, abs_path]
				(const PdmGitWorker::Result &last_result) {
				if (!last_result.ok) {
					fail(tr("準備工作區失敗"), last_result.output);
					return;
				}
				setFileWritable(abs_path, true);
				// 出庫=開始編輯:產生新小版號寫入圖框(入庫沿用、
				// 取消出庫會還原),標記「編輯中」,並清掉上一輪的
				// 確認者/核准者(新版次舊簽核作廢)。
				FileState fs;
				readDocFields(abs_path, &fs);
				stampDocFields(abs_path,
					QString::fromUtf8(DOC_STATUS_EDITING),
					nextMinor(fs.revision), true,
					{{QStringLiteral("checked-by"), QString()},
					 {QStringLiteral("approved-by"), QString()}});
				emit requestOpenFile(abs_path);
				refresh();
			};

			if (QDir(worktree).exists()) {
				// 既有工作區:更新到分支最新即可
				m_git->enqueue({"pull", "--ff-only"}, worktree,
					open_and_refresh);
			} else if (state.has_work_branch) {
				// 前次入庫未發行的延續:接回遠端 work 分支
				m_git->enqueue({"worktree", "add", "--track",
					"-b", branch, worktree,
					QStringLiteral("origin/") + branch},
					vault, open_and_refresh);
			} else {
				m_git->enqueue({"worktree", "add", "-b", branch,
					worktree,
					QStringLiteral("origin/") + DEFAULT_BRANCH},
					vault, open_and_refresh);
			}
		});
}

void PdmDialog::checkIn()
{
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->data(0, Qt::UserRole).toString();
	const QString branch = workBranchOf(rel_path);
	const QString worktree = worktreeDir(sanitizedStem(rel_path));
	const QString vault = vaultDir();

	if (!QDir(worktree).exists()) {
		QMessageBox::warning(this, tr("入庫"),
			tr("找不到本機工作區,無法入庫。\n"
			   "若確定不需保留修改,請改用「取消出庫」解除鎖定。"));
		return;
	}

	bool accepted = false;
	const QString message = QInputDialog::getMultiLineText(this,
		tr("入庫"), tr("變更說明(必填):"), QString(), &accepted);
	if (!accepted) return;
	if (message.trimmed().isEmpty()) {
		QMessageBox::warning(this, tr("入庫"), tr("變更說明不可空白。"));
		return;
	}

	// 入庫前先請編輯器把該檔存檔(同步),使用者不用手動 Cmd+S,
	// git 才抓得到最新編輯內容。
	emit requestSaveFile(worktree + '/' + rel_path);

	showBusy(true);
	// 版本已在出庫時寫入圖框、入庫直接沿用,不再進版。存檔後直接提交。
	m_git->enqueue({"add", "--", rel_path}, worktree, {});
	m_git->enqueue({"commit", "-m", message.trimmed()}, worktree,
		[this, rel_path, branch, worktree, vault]
		(const PdmGitWorker::Result &result) {
			const bool nothing_to_commit = !result.ok
				&& result.output.contains(QStringLiteral("nothing"));
			if (!result.ok && !nothing_to_commit) {
				fail(tr("入庫 commit 失敗"), result.output);
				return;
			}
			m_git->enqueue({"push", "-u", "origin", branch}, worktree,
				[this, rel_path, vault, worktree]
				(const PdmGitWorker::Result &push_result) {
					if (!push_result.ok) {
						// commit 已成、push 未成:保留鎖與工作區,
						// 使用者可稍後重按「入庫」重試(空變更會走
						// nothing-to-commit 路徑直接再 push)
						fail(tr("推送失敗,已保留出庫狀態,"
							"請檢查網路後重試入庫"),
						     push_result.output);
						return;
					}
					m_git->enqueue({"lfs", "unlock", rel_path},
						vault,
						[this, rel_path, worktree]
						(const PdmGitWorker::Result &r) {
							if (!r.ok) {
								fail(tr("解除鎖定失敗"),
								     r.output);
							}
							// 解鎖後把舊工作區副本設回唯讀,
							// 避免使用者繼續改到過期版本
							setFileWritable(worktree + '/'
								+ rel_path, false);
							// 入庫後關掉編輯器裡的該檔:磁碟已被
							// 戳記/commit,留著會是舊內容,下次出庫
							// 才能開到乾淨的最新版
							emit requestCloseFile(
								worktree + '/' + rel_path);
							refresh();
						});
				});
		});
}

void PdmDialog::cancelCheckOut()
{
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->data(0, Qt::UserRole).toString();
	const QString worktree = worktreeDir(sanitizedStem(rel_path));

	const auto answer = QMessageBox::question(this, tr("取消出庫"),
		tr("將捨棄「%1」出庫後的所有未入庫修改並解除鎖定,確定?")
			.arg(rel_path));
	if (answer != QMessageBox::Yes) return;

	showBusy(true);
	if (QDir(worktree).exists()) {
		m_git->enqueue({"checkout", "--", "."}, worktree, {});
	}
	m_git->enqueue({"lfs", "unlock", rel_path}, vaultDir(),
		[this, rel_path, worktree](const PdmGitWorker::Result &result) {
			if (!result.ok) fail(tr("解除鎖定失敗"), result.output);
			setFileWritable(worktree + '/' + rel_path, false);
			// 取消出庫後同樣關掉編輯器裡的該檔(已還原成庫內版本)
			emit requestCloseFile(worktree + '/' + rel_path);
			refresh();
		});
}

void PdmDialog::submitForReview()
{
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->data(0, Qt::UserRole).toString();
	const QString stem = sanitizedStem(rel_path);

	bool accepted = false;
	const QString body = QInputDialog::getMultiLineText(this,
		tr("送審"), tr("送審說明(必填,審核者會看到):"),
		QString(), &accepted);
	if (!accepted) return;
	if (body.trimmed().isEmpty()) {
		QMessageBox::warning(this, tr("送審"), tr("送審說明不可空白。"));
		return;
	}

	showBusy(true);

	const QString branch = workBranchOf(rel_path);
	const QString worktree = worktreeDir(sanitizedStem(rel_path));
	const QString abs_path = worktree + '/' + rel_path;

	auto open_pull_request = [this, rel_path, stem, branch,
				  body = body.trimmed()]() {
		m_service->createPullRequest(currentRepoFullName(),
			branch, DEFAULT_BRANCH,
			tr("%1 送審").arg(stem), body,
			[this, rel_path](const PdmService::Reply &reply) {
				showBusy(false);
				if (!reply.ok) {
					fail(tr("送審失敗"), reply.error);
					return;
				}
				const int pr = reply.json.object()
					.value(QStringLiteral("number")).toInt();
				m_status_label->setText(
					tr("「%1」已送審(#%2)")
						.arg(rel_path).arg(pr));
				refresh();
			});
	};

	// 送審前把文件狀態標記為「審核中」寫入圖框並推上 work 分支,
	// 讓審核者看到的版本即帶此狀態。工作區不在(換機送審)則略過。
	if (QDir(worktree).exists()
	    && stampDocFields(abs_path,
			      QString::fromUtf8(DOC_STATUS_REVIEWING),
			      QString(), false)) {   // 保留入庫時給的版本
		setFileWritable(abs_path, true);
		m_git->enqueue({"add", "--", rel_path}, worktree, {});
		m_git->enqueue({"commit", "-m",
			QStringLiteral("文件狀態：審核中")}, worktree, {});
		m_git->enqueue({"push", "origin", branch}, worktree,
			[open_pull_request](const PdmGitWorker::Result &r) {
				Q_UNUSED(r);   // 推送失敗不擋送審,PR 仍以現有 head 建立
				open_pull_request();
			});
	} else {
		open_pull_request();
	}
}

void PdmDialog::openReviewView()
{
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->data(0, Qt::UserRole).toString();
	const FileState state = m_files.value(rel_path);
	if (state.pr_index <= 0) return;
	const QString vault = vaultDir();
	const QString review_dir = reviewDir(state.pr_index);
	const QString review_ref = QStringLiteral("refs/pdm/pr-%1")
		.arg(state.pr_index);

	showBusy(true);
	// 審的必須是 PR head 的固定 commit;獨立 worktree,與任何編輯
	// 工作區隔離。檔案未被鎖定,lockable 會讓它在磁碟上保持唯讀,
	// QETProject 開啟時自然進入唯讀模式(見開發計畫附錄 A)。
	m_git->enqueue({"fetch", "origin",
		QStringLiteral("+refs/pull/%1/head:%2")
			.arg(state.pr_index).arg(review_ref)},
		vault, {});
	auto open_review = [this, rel_path, review_dir]
		(const PdmGitWorker::Result &result) {
		if (!result.ok) {
			fail(tr("準備審核檢視失敗"), result.output);
			return;
		}
		// 審核對象強制唯讀(不依賴 lfs 的 lockable 行為)
		setFileWritable(review_dir + '/' + rel_path, false);
		emit requestOpenFile(review_dir + '/' + rel_path);
		showBusy(false);
	};
	if (QDir(review_dir).exists()) {
		m_git->enqueue({"checkout", "--detach", review_ref},
			review_dir, open_review);
	} else {
		m_git->enqueue({"worktree", "add", "--detach",
			review_dir, review_ref}, vault, open_review);
	}
}

void PdmDialog::viewReleased()
{
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->data(0, Qt::UserRole).toString();
	const QString vault = vaultDir();
	// main 上最後發行版匯出成唯讀暫存檔開啟,與任何編輯工作區完全隔離;
	// 別人正在編輯(work 分支)也不影響,這裡只讀 main 的內容。
	const QString out = QDir::tempPath() + QStringLiteral("/pdm-released-")
		+ sanitizedStem(rel_path) + QStringLiteral(".qet");

	showBusy(true);
	m_git->enqueue({"show", QStringLiteral("origin/") + DEFAULT_BRANCH
			+ ':' + rel_path}, vault,
		[this, rel_path, out](const PdmGitWorker::Result &result) {
			showBusy(false);
			if (!result.ok) {
				fail(tr("讀取發行版失敗"), result.output);
				return;
			}
			QFile file(out);
			if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
				fail(tr("無法建立暫存檔"), out);
				return;
			}
			QTextStream stream(&file);
			stream << result.output;
			file.close();
			// 強制唯讀:QETProject 開啟唯讀檔時自動進入唯讀模式
			setFileWritable(out, false);
			emit requestOpenFile(out);
			m_status_label->setText(
				tr("已開啟「%1」發行版(唯讀)").arg(rel_path));
		});
}

void PdmDialog::confirmDone()
{
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->data(0, Qt::UserRole).toString();
	const FileState state = m_files.value(rel_path);
	if (state.pr_index <= 0) return;

	bool accepted = false;
	const QString note = QInputDialog::getMultiLineText(this,
		tr("確認完畢"), tr("簽核訊息(必填,會寫入 commit):"),
		QString(), &accepted);
	if (!accepted) return;
	if (note.trimmed().isEmpty()) {
		QMessageBox::warning(this, tr("確認完畢"), tr("簽核訊息不可空白。"));
		return;
	}

	// 寫確認者 + commit(帶簽核訊息)到 work 分支,不核准、不合併
	const int pr_index = state.pr_index;
	signoffOnWorkBranch(rel_path, QString(),
		{{QStringLiteral("checked-by"), m_username}},
		tr("確認者 %1 已確認完畢：%2").arg(m_username, note.trimmed()),
		[this, rel_path, pr_index]() {
			showBusy(false);
			m_status_label->setText(
				tr("「%1」已確認完畢").arg(rel_path));
			// 確認完畢後自動關掉開著的審核檢視(唯讀,留著沒意義)
			emit requestCloseFile(reviewDir(pr_index) + '/' + rel_path);
			refresh();
		});
}

void PdmDialog::rejectReview()
{
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->data(0, Qt::UserRole).toString();
	const FileState state = m_files.value(rel_path);
	if (state.pr_index <= 0) return;
	const int pr_index = state.pr_index;

	bool accepted = false;
	const QString reason = QInputDialog::getMultiLineText(this,
		tr("退回"), tr("退回意見(必填,會寫入 commit,製圖者會看到):"),
		QString(), &accepted);
	if (!accepted) return;
	if (reason.trimmed().isEmpty()) {
		QMessageBox::warning(this, tr("退回"), tr("退回意見不可空白。"));
		return;
	}
	const QString trimmed = reason.trimmed();

	// 寫「退回修改」狀態 + commit(帶退回意見)到 work 分支,再送 REQUEST_CHANGES
	signoffOnWorkBranch(rel_path, QString::fromUtf8(DOC_STATUS_REJECTED),
		QMap<QString, QString>(),
		tr("退回 by %1：%2").arg(m_username, trimmed),
		[this, rel_path, pr_index, trimmed]() {
			m_service->submitReview(currentRepoFullName(), pr_index,
				QStringLiteral("REQUEST_CHANGES"), trimmed,
				[this, rel_path](const PdmService::Reply &reply) {
					showBusy(false);
					if (!reply.ok) {
						fail(tr("退回失敗"), reply.error);
						return;
					}
					m_status_label->setText(
						tr("「%1」已退回").arg(rel_path));
					refresh();
				});
		});
}

void PdmDialog::approveAndRelease()
{
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->data(0, Qt::UserRole).toString();
	const FileState state = m_files.value(rel_path);
	if (state.pr_index <= 0) return;
	const int pr_index = state.pr_index;
	const QString stem = sanitizedStem(rel_path);
	const QString vault = vaultDir();

	bool accepted = false;
	const QString note = QInputDialog::getMultiLineText(this,
		tr("核准發行"), tr("簽核訊息(必填,會寫入 commit):"),
		QString(), &accepted);
	if (!accepted) return;
	if (note.trimmed().isEmpty()) {
		QMessageBox::warning(this, tr("核准發行"), tr("簽核訊息不可空白。"));
		return;
	}
	const QString msg = note.trimmed();

	// 先在 work 分支寫核准者 + 已發行狀態 + commit,再核准最新 head 並合併發行。
	// 順序:先 commit(改 head)、後核准 → 不觸發「廢止過時核准」。
	signoffOnWorkBranch(rel_path, QString::fromUtf8(DOC_STATUS_RELEASED),
		{{QStringLiteral("approved-by"), m_username}},
		tr("核准發行 by %1：%2").arg(m_username, msg),
		[this, rel_path, pr_index, stem, vault, msg]() {
		// 推下一發行版次 → 合併 → 發行(核准成功或自我核准被拒都走這)
		auto merge_and_release = [this, rel_path, pr_index, stem, vault]() {
			m_git->enqueue({"ls-remote", "--tags", "origin",
				QStringLiteral("refs/tags/release/%1-v*").arg(stem)},
				vault,
				[this, rel_path, pr_index, stem, vault]
				(const PdmGitWorker::Result &tags_result) {
				int next_version = 1;
				const QRegularExpression pattern(
					QStringLiteral("refs/tags/release/%1-v(\\d+)")
					.arg(QRegularExpression::escape(stem)));
				auto matches = pattern.globalMatch(tags_result.output);
				while (matches.hasNext()) {
					next_version = qMax(next_version,
						matches.next().captured(1).toInt() + 1);
				}
				const QString tag = QStringLiteral("release/%1-v%2")
					.arg(stem).arg(next_version);
				m_service->mergePullRequest(currentRepoFullName(),
					pr_index,
					[this, rel_path, stem, tag, vault]
					(const PdmService::Reply &merge_reply) {
					if (!merge_reply.ok) {
						fail(tr("合併失敗(需有效核准,且僅"
							"核准者有權發行)"),
						     merge_reply.error);
						return;
					}
					m_git->enqueue({"fetch", "origin", "--prune"},
						vault, {});
					m_git->enqueue({"checkout", DEFAULT_BRANCH},
						vault, {});
					m_git->enqueue({"pull", "--ff-only"}, vault, {});
					m_git->enqueue({"rev-parse", "HEAD"}, vault,
						[this, rel_path, stem, tag]
						(const PdmGitWorker::Result &sha_result) {
						finishRelease(rel_path, stem, tag,
							sha_result.output.trimmed());
					});
				});
			});
		};
		// 核准含核准者 commit 的最新 head
		m_service->submitReview(currentRepoFullName(), pr_index,
			QStringLiteral("APPROVED"), msg,
			[this, merge_and_release]
			(const PdmService::Reply &approve_reply) {
			if (!approve_reply.ok) {
				// 開發期單人測試:Gitea 擋「核准自己的 PR」(422),
				// 跳過核准直接嘗試合併(需 main 分支保護暫免必要核准)。
				if (approve_reply.http_status == 422) {
					merge_and_release();
					return;
				}
				showBusy(false);
				fail(tr("核准失敗"), approve_reply.error);
				return;
			}
			merge_and_release();
		});
	});
}

/**
	發行第二段:從合併後的 main 渲染 PDF → 建 Release → 上傳附件 →
	清理 work 分支與 worktree。渲染一律出自 vault(= tag 內容),
	不用任何人的工作區(開發計畫 §3.6)。
*/
void PdmDialog::finishRelease(const QString &rel_path, const QString &stem,
				  const QString &tag, const QString &sha)
{
	const QString vault = vaultDir();
	const QString pdf_path = QDir::tempPath() + '/' + stem + '-'
		+ tag.section('-', -1) + QStringLiteral(".pdf");

	m_git->enqueueProgram(QCoreApplication::applicationFilePath(),
		{QStringLiteral("--cli-export-pdf"), vault + '/' + rel_path,
		 QStringLiteral("--out"), pdf_path},
		vault,
		[this, rel_path, tag, sha, pdf_path]
		(const PdmGitWorker::Result &render_result) {
		// 渲染失敗不擋發行(tag 內容才是正本),但要讓使用者知道
		const bool has_pdf = render_result.ok
			&& QFileInfo::exists(pdf_path);
		const QString body = tr("圖檔:%1\n來源 commit:%2\n"
			"由 QET 圖檔管理發行%3")
			.arg(rel_path, sha,
			     has_pdf ? QString()
				     : tr("(PDF 渲染失敗,無附件)"));
		m_service->createRelease(currentRepoFullName(), tag,
			QLatin1String(DEFAULT_BRANCH),
			tag, body,
			[this, rel_path, tag, pdf_path, has_pdf]
			(const PdmService::Reply &release_reply) {
			if (!release_reply.ok) {
				fail(tr("建立發行失敗"), release_reply.error);
				return;
			}
			const qint64 release_id = static_cast<qint64>(
				release_reply.json.object()
					.value(QStringLiteral("id")).toDouble());
			auto cleanup = [this, rel_path, tag]() {
				const QString worktree =
					worktreeDir(sanitizedStem(rel_path));
				m_git->enqueue({"worktree", "remove", "--force",
					worktree}, vaultDir(), {});
				m_git->enqueue({"branch", "-D",
					workBranchOf(rel_path)}, vaultDir(),
					[this, rel_path, tag]
					(const PdmGitWorker::Result &) {
						m_status_label->setText(
							tr("「%1」已發行:%2")
							.arg(rel_path, tag));
						refresh();
					});
			};
			if (has_pdf) {
				m_service->uploadReleaseAsset(
					currentRepoFullName(), release_id,
					pdf_path,
					[this, cleanup]
					(const PdmService::Reply &asset_reply) {
						if (!asset_reply.ok) {
							fail(tr("PDF 附件上傳失敗"
								"(發行已建立)"),
							     asset_reply.error);
						}
						cleanup();
					});
			} else {
				cleanup();
			}
		});
	});
}

void PdmDialog::forceUnlock()
{
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->data(0, Qt::UserRole).toString();
	const FileState state = m_files.value(rel_path);

	const auto answer = QMessageBox::warning(this, tr("強制解鎖"),
		tr("「%1」目前由 %2 鎖定。\n強制解鎖會使其未入庫的修改"
		   "無法入庫,僅限管理員處理卡鎖(休假/離職/電腦故障)時使用。\n"
		   "確定強制解鎖?").arg(rel_path, state.lock_owner),
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
	if (answer != QMessageBox::Yes) return;

	showBusy(true);
	m_git->enqueue({"lfs", "unlock", "--force", rel_path}, vaultDir(),
		[this, rel_path](const PdmGitWorker::Result &result) {
			if (!result.ok) {
				fail(tr("強制解鎖失敗(需 repo 管理員權限)"),
				     result.output);
			} else {
				m_status_label->setText(
					tr("「%1」已強制解鎖").arg(rel_path));
			}
			refresh();
		});
}

void PdmDialog::revertToRelease()
{
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->data(0, Qt::UserRole).toString();
	const FileState state = m_files.value(rel_path);
	if (!state.has_work_branch) return;
	const QString branch = workBranchOf(rel_path);
	const QString worktree = worktreeDir(sanitizedStem(rel_path));
	const QString vault = vaultDir();

	QMessageBox box(QMessageBox::Warning, tr("退回上一發行版"),
		tr("退回「%1」到上一發行版?").arg(QFileInfo(rel_path).fileName()),
		QMessageBox::Yes | QMessageBox::No, this);
	box.setInformativeText(tr("會捨棄進行中的所有修改,無法復原。"));
	box.setDefaultButton(QMessageBox::No);
	if (box.exec() != QMessageBox::Yes) return;

	showBusy(true);
	// 1. 解鎖(若有鎖):自己的一般解鎖,他人的強制解鎖(核准者才會走到)
	if (state.lock_owner == m_username && !m_username.isEmpty())
		m_git->enqueue({"lfs", "unlock", rel_path}, vault, {});
	else if (!state.lock_owner.isEmpty())
		m_git->enqueue({"lfs", "unlock", "--force", rel_path}, vault, {});
	// 2. 移除工作區(才能刪本地分支)
	if (QDir(worktree).exists())
		m_git->enqueue({"worktree", "remove", "--force", worktree},
			vault, {});
	// 3. 刪本地 work 分支
	m_git->enqueue({"branch", "-D", branch}, vault, {});
	// 4. 刪遠端 work 分支(會一併關閉其 PR),關檔並重整
	m_git->enqueue({"push", "origin", "--delete", branch}, vault,
		[this, rel_path, worktree](const PdmGitWorker::Result &r) {
			if (!r.ok) {
				fail(tr("刪除工作分支失敗"), r.output);
				return;
			}
			emit requestCloseFile(worktree + '/' + rel_path);
			m_status_label->setText(
				tr("「%1」已退回上一發行版").arg(rel_path));
			refresh();
		});
}

void PdmDialog::mutateMain(const std::function<void ()> &change,
			  const QString &commit_message)
{
	const QString vault = vaultDir();
	showBusy(true);
	// 準備:更新到 origin/main 最新且乾淨
	m_git->enqueue({"fetch", "origin", "--prune"}, vault, {});
	m_git->enqueue({"checkout", DEFAULT_BRANCH}, vault, {});
	m_git->enqueue({"reset", "--hard",
		QStringLiteral("origin/") + DEFAULT_BRANCH}, vault,
		[this, change, commit_message, vault]
		(const PdmGitWorker::Result &r) {
		if (!r.ok) { fail(tr("準備圖庫失敗"), r.output); return; }
		change();   // 呼叫端做檔案異動 + git add/rm(以 enqueue 排入)
		m_git->enqueue({"commit", "-m", commit_message}, vault, {});
		m_git->enqueue({"push", "origin", DEFAULT_BRANCH}, vault,
			[this](const PdmGitWorker::Result &pr) {
			if (!pr.ok) {
				fail(tr("推送 main 失敗(核准者需在 main 分支保護的"
					"可推送白名單內)"), pr.output);
				return;
			}
			m_status_label->setText(tr("圖庫已更新"));
			refresh();
		});
	});
}

void PdmDialog::addFolder()
{
	if (!m_is_releaser) return;
	bool ok = false;
	QString name = QInputDialog::getText(this, tr("新增資料夾"),
		tr("資料夾名稱(勿含空白):"), QLineEdit::Normal, QString(), &ok);
	if (!ok) return;
	name = name.trimmed();
	name.replace(QLatin1Char(' '), QLatin1Char('-'));
	if (name.isEmpty()) return;
	if (m_extra_folders.contains(name)) {
		QMessageBox::warning(this, tr("新增資料夾"),
			tr("資料夾「%1」已存在。").arg(name));
		return;
	}
	const QString vault = vaultDir();
	const QString keep = name + QStringLiteral("/.gitkeep");
	mutateMain([this, vault, name, keep]() {
		QDir(vault).mkpath(name);
		QFile f(vault + '/' + keep);
		if (f.open(QIODevice::WriteOnly)) f.close();
		m_git->enqueue({"add", "--", keep}, vault, {});
	}, tr("新增資料夾：%1").arg(name));
}

void PdmDialog::deleteFolder()
{
	if (!m_is_releaser) return;
	QTreeWidgetItem *item = m_folder_tree->currentItem();
	if (!item) return;
	const QString folder = item->text(0);
	if (folder == tr("(根目錄)")) return;

	// 資料夾內若有進行中的圖檔則擋下
	for (auto it = m_files.constBegin(); it != m_files.constEnd(); ++it) {
		if (QFileInfo(it.key()).path() != folder) continue;
		if (!it->lock_owner.isEmpty() || it->has_work_branch
		    || it->pr_index > 0) {
			QMessageBox::warning(this, tr("刪除資料夾"),
				tr("資料夾內有進行中的圖檔(出庫/送審),"
				   "請先處理完再刪除。"));
			return;
		}
	}
	if (QMessageBox::warning(this, tr("刪除資料夾"),
		tr("將永久刪除資料夾「%1」及其下所有圖檔,確定?").arg(folder),
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
	    != QMessageBox::Yes)
		return;

	const QString vault = vaultDir();
	mutateMain([this, vault, folder]() {
		m_git->enqueue({"rm", "-r", "--", folder}, vault, {});
	}, tr("刪除資料夾：%1").arg(folder));
}

void PdmDialog::deleteDrawing()
{
	if (!m_is_releaser) return;
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->data(0, Qt::UserRole).toString();
	const FileState state = m_files.value(rel_path);
	if (!state.lock_owner.isEmpty() || state.has_work_branch
	    || state.pr_index > 0) {
		QMessageBox::warning(this, tr("刪除圖檔"),
			tr("「%1」有進行中的出庫/送審,請先「退回上一發行版」"
			   "再刪除。").arg(rel_path));
		return;
	}
	if (QMessageBox::warning(this, tr("刪除圖檔"),
		tr("將從圖庫刪除「%1」(git 歷史與發行 tag 仍保留),確定?")
			.arg(rel_path),
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
	    != QMessageBox::Yes)
		return;

	const QString vault = vaultDir();
	mutateMain([this, vault, rel_path]() {
		m_git->enqueue({"rm", "--", rel_path}, vault, {});
	}, tr("刪除圖檔：%1").arg(rel_path));
}

void PdmDialog::loadReleaseHistory(const QString &rel_path)
{
	const QString stem = sanitizedStem(rel_path);
	const FileState st = m_files.value(rel_path);
	// 進行中的圖檔看 work 分支(含未發行的最新 commit),否則看 main
	const QString ref = st.has_work_branch
		? QStringLiteral("origin/") + workBranchOf(rel_path)
		: QStringLiteral("origin/") + QLatin1String(DEFAULT_BRANCH);
	const QString vault = vaultDir();
	// %H hash, %s subject, %cd 日期時間, %cn 提交者, %D refs
	m_git->enqueue({QStringLiteral("log"),
		QStringLiteral("--date=format:%Y-%m-%d %H:%M"),
		QStringLiteral("--format=%H%x1f%s%x1f%cd%x1f%cn%x1f%D"),
		ref, QStringLiteral("--"), rel_path}, vault,
		[this, rel_path, stem, vault](const PdmGitWorker::Result &r) {
		const QTreeWidgetItem *current = selectedFileItem();
		if (!current
		    || current->data(0, Qt::UserRole).toString() != rel_path)
			return;
		if (!r.ok) { m_history_tree->clear(); return; }

		struct Rec { QString subject, datetime, committer, rel_tag, version; };
		auto recs = std::make_shared<QVector<Rec>>();
		auto hashes = std::make_shared<QStringList>();
		const QString rel_prefix = QStringLiteral("release/%1-v").arg(stem);
		const QStringList lines = r.output.split('\n', Qt::SkipEmptyParts);
		for (const QString &line : lines) {
			const QStringList f = line.split(QChar(0x1f));
			if (f.size() < 5) continue;
			Rec rec;
			rec.subject   = f.at(1);
			rec.datetime  = f.at(2);
			rec.committer = f.at(3);
			const QStringList tokens = f.at(4).split(QLatin1Char(','));
			for (const QString &token : tokens) {
				const QString t = token.trimmed();
				if (t.startsWith(QLatin1String("tag: "))) {
					const QString tg = t.mid(5);
					if (tg.startsWith(rel_prefix)) {
						rec.rel_tag = tg;
						break;
					}
				}
			}
			recs->append(rec);
			hashes->append(f.at(0));
		}
		if (recs->isEmpty()) { m_history_tree->clear(); return; }

		// 逐 commit 讀出當下的 indexrev 當版本號(出庫領號、每次入庫都有),
		// 全部讀完再一次建樹
		auto remaining = std::make_shared<int>(recs->size());
		for (int i = 0; i < recs->size(); ++i) {
			m_git->enqueue({QStringLiteral("show"),
				hashes->at(i) + QLatin1Char(':') + rel_path}, vault,
				[this, rel_path, stem, recs, remaining, i]
				(const PdmGitWorker::Result &sr) {
				QDomDocument doc;
				if (doc.setContent(sr.output))
					(*recs)[i].version = doc.documentElement()
						.firstChildElement(QStringLiteral("diagram"))
						.attribute(QStringLiteral("indexrev"));
				if (--(*remaining) != 0) return;

				const QTreeWidgetItem *cur = selectedFileItem();
				if (!cur
				    || cur->data(0, Qt::UserRole).toString() != rel_path)
					return;
				m_history_tree->clear();
				const QColor highlight(255, 244, 214);
				const QString pfx =
					QStringLiteral("release/%1-v").arg(stem);
				auto *pending = new QTreeWidgetItem(m_history_tree,
					{tr("編輯中"), QString(), QString(),
					 tr("(未發行)")});
				QTreeWidgetItem *parent = pending;
				for (const Rec &rec : *recs) {
					if (!rec.rel_tag.isEmpty()) {
						// 發行版:★ + 版本 + 底色粗體,可雙擊唯讀開啟
						auto *item = new QTreeWidgetItem(
							m_history_tree,
							{QStringLiteral("★ ") + rec.version,
							 rec.datetime, rec.committer,
							 tr("正式發行")});
						for (int c = 0; c < 4; ++c) {
							item->setBackground(c, highlight);
							QFont fnt = item->font(c);
							fnt.setBold(true);
							item->setFont(c, fnt);
						}
						item->setData(0, Qt::UserRole, rec.rel_tag);
						parent = item;
					} else {
						new QTreeWidgetItem(parent,
							{rec.version, rec.datetime,
							 rec.committer, rec.subject});
					}
				}
				if (pending->childCount() == 0) delete pending;
				m_history_tree->expandAll();
			});
		}
	});
}

void PdmDialog::openReleaseRevision(const QString &tag, const QString &rel_path)
{
	if (!tag.startsWith(QStringLiteral("release/"))) return;   // 略過佔位列

	// 發行版以 detached worktree 唯讀開啟(lockable 保唯讀)
	const QString vault = vaultDir();
	QString dir_name = tag;
	dir_name.replace('/', '_');
	const QString view_dir = PdmSettings::workRoot() + '/'
		+ currentRepoFullName()
		+ QStringLiteral("/releases-view/") + dir_name;
	showBusy(true);
	m_git->enqueue({"fetch", "origin", "--tags"}, vault, {});
	auto open_view = [this, rel_path, view_dir]
		(const PdmGitWorker::Result &result) {
		if (!result.ok) {
			fail(tr("開啟發行版失敗"), result.output);
			return;
		}
		setFileWritable(view_dir + '/' + rel_path, false);
		emit requestOpenFile(view_dir + '/' + rel_path);
		showBusy(false);
	};
	if (QDir(view_dir).exists()) {
		open_view({true, 0, QString()});
	} else {
		m_git->enqueue({"worktree", "add", "--detach",
			view_dir, tag}, vault, open_view);
	}
}

QString PdmDialog::friendlyStep(const QString &cmd)
{
	// 把 git/子行程指令對應成使用者看得懂的階段說明
	struct Rule { const char *needle; const char *text; };
	static const Rule rules[] = {
		{"clone",            "首次下載圖庫…"},
		{"lfs lock",         "鎖定圖檔(出庫)…"},
		{"lfs unlock",       "解除鎖定…"},
		{"worktree add",     "建立本機工作區…"},
		{"worktree remove",  "清理工作區…"},
		{"fetch",            "連線伺服器、取得最新版本…"},
		{"pull",             "更新本機到最新…"},
		{"push",             "上傳到伺服器…"},
		{"commit",           "記錄變更…"},
		{"add ",             "準備變更…"},
		{"checkout",         "切換版本…"},
		{"reset",            "還原工作區…"},
		{"restore",          "還原工作區…"},
		{"merge",            "合併發行…"},
		{"show",             "讀取最新版…"},
		{"ls-remote",        "查詢版本標籤…"},
		{"rev-parse",        "確認版本…"},
		{"cli-export-pdf",   "產生發行 PDF…"},
		{"cli-validate",     "驗證圖檔…"},
	};
	for (const Rule &r : rules) {
		if (cmd.contains(QLatin1String(r.needle)))
			return tr(r.text);
	}
	return tr("處理中…");
}

void PdmDialog::ensureBusyDialog()
{
	if (m_busy_dialog) return;
	// 父層設為本對話框的 parent(編輯器主視窗):圖檔管理視窗沒開也能置中顯示
	QWidget *host = parentWidget() ? parentWidget() : this;
	// 非模態 + 置頂:提供進度回饋但不鎖死整個程式(避免某步驟未收尾時
	// 卡住其他操作,如刪資料夾)。收起一律由 git 佇列清空(allFinished)負責。
	m_busy_dialog = new QDialog(host,
		Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint
		| Qt::WindowStaysOnTopHint);
	m_busy_dialog->setWindowTitle(tr("圖檔管理"));
	m_busy_dialog->setModal(false);
	m_busy_dialog->setFixedWidth(380);

	auto *lay = new QVBoxLayout(m_busy_dialog);
	lay->setContentsMargins(28, 24, 28, 24);
	lay->setSpacing(16);

	m_busy_label = new QLabel(tr("處理中…"), m_busy_dialog);
	m_busy_label->setWordWrap(true);
	QFont f = m_busy_label->font();
	f.setPointSizeF(f.pointSizeF() * 1.15);
	f.setBold(true);
	m_busy_label->setFont(f);
	m_busy_label->setAlignment(Qt::AlignHCenter);
	lay->addWidget(m_busy_label);

	auto *bar = new QProgressBar(m_busy_dialog);
	bar->setRange(0, 0);          // 忙碌動畫(不確定進度)
	bar->setTextVisible(false);
	bar->setFixedHeight(6);
	lay->addWidget(bar);
}

void PdmDialog::showBusy(bool busy)
{
	// 進度對話框改由 git 佇列生命週期(stepStarted/allFinished)驅動,
	// 這裡只管主視窗內嵌進度條與按鈕啟用。
	if (busy) {
		m_progress->setRange(0, 0);
		m_progress->show();
	} else {
		m_progress->hide();
	}
	m_refresh_button->setEnabled(!busy);
	m_repo_combo->setEnabled(!busy);
}

void PdmDialog::fail(const QString &title, const QString &log)
{
	if (m_busy_dialog) m_busy_dialog->hide();
	showBusy(false);
	m_status_label->setText(title);
	QMessageBox::warning(this, title, log.right(1500));
}

QString PdmDialog::currentRepoFullName() const
{
	return m_repo_combo->currentText();
}

QString PdmDialog::vaultDir() const
{
	return PdmSettings::workRoot() + '/' + currentRepoFullName()
		+ QStringLiteral("/vault");
}

QString PdmDialog::worktreeDir(const QString &stem) const
{
	return PdmSettings::workRoot() + '/' + currentRepoFullName()
		+ QStringLiteral("/checkouts/") + stem;
}

QString PdmDialog::reviewDir(int pr_index) const
{
	return PdmSettings::workRoot() + '/' + currentRepoFullName()
		+ QStringLiteral("/reviews/pr-%1").arg(pr_index);
}

QString PdmDialog::remoteUrlWithCredentials() const
{
	// TODO:token 會留在 vault 的 .git/config;內網暫可接受,
	// 之後改 git credential helper(見開發計畫注意事項 8)。
	QUrl url(PdmSettings::serverUrl());
	url.setUserName(m_username);
	url.setPassword(PdmSettings::token());
	url.setPath('/' + currentRepoFullName() + QStringLiteral(".git"));
	return url.toString(QUrl::FullyEncoded);
}

QTreeWidgetItem *PdmDialog::selectedFileItem() const
{
	return m_tree->currentItem();
}

PdmDialog::FileState PdmDialog::selectedState() const
{
	const QTreeWidgetItem *item = selectedFileItem();
	return item ? m_files.value(item->data(0, Qt::UserRole).toString()) : FileState();
}

PdmDialog::OpenContext PdmDialog::openContext(const QString &abs_path) const
{
	const QString base = PdmSettings::workRoot() + '/' + currentRepoFullName();
	if (abs_path.startsWith(base + QStringLiteral("/checkouts/")))
		return CheckoutEdit;
	if (abs_path.startsWith(base + QStringLiteral("/reviews/")))
		return ReviewReadOnly;
	return NotManaged;
}

bool PdmDialog::isManagedPath(const QString &abs_path)
{
	const QString root = PdmSettings::workRoot();
	if (!root.isEmpty() && abs_path.startsWith(root + '/'))
		return true;
	// 唯讀檢視匯出到暫存目錄的唯讀 .qet:發行版 pdm-released-、最新版 pdm-latest-
	const QString name = QFileInfo(abs_path).fileName();
	return name.startsWith(QStringLiteral("pdm-released-"))
	    || name.startsWith(QStringLiteral("pdm-latest-"));
}

QString PdmDialog::drawingRelPath(const QString &abs_path) const
{
	// 1) 工作區內:找 /checkouts/ 或 /reviews/ 之後的 <stem 或 pr-N>/<rel_path>,
	//    取 rel_path。不依賴 repo 下拉(啟動早期尚未載入),用路徑標記即可。
	for (const QString &marker : {QStringLiteral("/checkouts/"),
				      QStringLiteral("/reviews/")}) {
		const int idx = abs_path.indexOf(marker);
		if (idx < 0) continue;
		const QString rest = abs_path.mid(idx + marker.length());
		const int slash = rest.indexOf(QLatin1Char('/'));
		if (slash >= 0) return rest.mid(slash + 1);
	}
	// 2) 唯讀檢視暫存(pdm-released-/pdm-latest-<檔名>.qet):以檔名比對圖庫清單,
	//    比對到才回傳完整相對路徑(供選取/抓取用);比不到回空。
	QString name = QFileInfo(abs_path).fileName();
	for (const QString &prefix : {QStringLiteral("pdm-released-"),
				      QStringLiteral("pdm-latest-")}) {
		if (name.startsWith(prefix)) { name = name.mid(prefix.length()); break; }
	}
	for (auto it = m_files.constBegin(); it != m_files.constEnd(); ++it) {
		if (QFileInfo(it.key()).fileName() == name)
			return it.key();
	}
	return QString();
}

bool PdmDialog::isCheckedOutByMe(const QString &abs_path) const
{
	if (openContext(abs_path) != CheckoutEdit) return false;
	const FileState st = m_files.value(drawingRelPath(abs_path));
	return !m_username.isEmpty() && st.lock_owner == m_username;
}

QString PdmDialog::relPathForOpen(const QString &abs_path) const
{
	const QString base = PdmSettings::workRoot() + '/' + currentRepoFullName();
	QString rest;
	if (abs_path.startsWith(base + QStringLiteral("/checkouts/")))
		rest = abs_path.mid((base + QStringLiteral("/checkouts/")).length());
	else if (abs_path.startsWith(base + QStringLiteral("/reviews/")))
		rest = abs_path.mid((base + QStringLiteral("/reviews/")).length());
	else
		return QString();
	// rest = <stem 或 pr-N>/<rel_path>,去掉第一段
	const int slash = rest.indexOf(QLatin1Char('/'));
	return slash >= 0 ? rest.mid(slash + 1) : QString();
}

bool PdmDialog::selectFileInUi(const QString &rel_path)
{
	if (rel_path.isEmpty() || !m_files.contains(rel_path)) return false;
	const QString dir = QFileInfo(rel_path).path();
	const QString folder = (dir == QLatin1String("."))
		? tr("(根目錄)") : dir;
	for (int i = 0; i < m_folder_tree->topLevelItemCount(); ++i) {
		if (m_folder_tree->topLevelItem(i)->text(0) == folder) {
			m_folder_tree->setCurrentItem(m_folder_tree->topLevelItem(i));
			break;
		}
	}
	// 選資料夾會同步觸發 populateFileList,清單重建後再選檔
	for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
		if (m_tree->topLevelItem(i)->data(0, Qt::UserRole).toString()
		    == rel_path) {
			m_tree->setCurrentItem(m_tree->topLevelItem(i));
			return true;
		}
	}
	return false;
}

void PdmDialog::checkInByPath(const QString &abs_path)
{
	if (selectFileInUi(drawingRelPath(abs_path))) checkIn();
}

void PdmDialog::checkOutByPath(const QString &abs_path)
{
	// 出庫的進度由專用進度對話框顯示(checkOut→showBusy),不必開主視窗。
	const QString rel = drawingRelPath(abs_path);
	if (!rel.isEmpty() && selectFileInUi(rel)) {
		checkOut();
		return;
	}
	// 清單尚未載入,或無法解析(如已從圖庫刪除的圖):
	// 開主視窗、重整清單讓使用者在清單中出庫。
	show();
	raise();
	activateWindow();
	refresh();
}

void PdmDialog::browseLatestByPath(const QString &abs_path)
{
	// 一律抓伺服器最新版(origin/main),不打開本機舊快取/暫存。
	const QString rel = drawingRelPath(abs_path);
	if (rel.isEmpty()) {
		show(); raise(); activateWindow(); refresh();
		return;
	}
	const QString vault = vaultDir();
	showBusy(true);   // 進度由專用對話框顯示(fetch→git show)
	m_git->enqueue({"fetch", "origin", "--prune"}, vault, {});
	const QString ref = QStringLiteral("origin/") + QLatin1String(DEFAULT_BRANCH)
		+ QLatin1Char(':') + rel;
	m_git->enqueue({"show", ref}, vault,
		[this, rel](const PdmGitWorker::Result &r) {
		showBusy(false);
		if (!r.ok) {
			fail(tr("無法瀏覽最新版"),
			     tr("此圖可能尚未發行到 %1。\n%2")
				.arg(QLatin1String(DEFAULT_BRANCH), r.output));
			return;
		}
		const QString tmp = QDir::tempPath() + QStringLiteral("/pdm-latest-")
			+ QFileInfo(rel).fileName();
		QFile f(tmp);
		if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			fail(tr("無法建立暫存檔"), tmp);
			return;
		}
		f.write(r.output.toUtf8());
		f.close();
		setFileWritable(tmp, false);
		emit requestOpenFile(tmp);
	});
}

void PdmDialog::cancelByPath(const QString &abs_path)
{
	if (selectFileInUi(drawingRelPath(abs_path))) cancelCheckOut();
}

void PdmDialog::confirmByPath(const QString &abs_path)
{
	if (selectFileInUi(drawingRelPath(abs_path))) confirmDone();
}

void PdmDialog::releaseByPath(const QString &abs_path)
{
	if (selectFileInUi(drawingRelPath(abs_path))) approveAndRelease();
}
