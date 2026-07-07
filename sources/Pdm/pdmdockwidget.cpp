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
#include "pdmdockwidget.h"

#include "pdmgitworker.h"
#include "pdmservice.h"
#include "pdmsettings.h"

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
	// Phase 1 固定用 main 當發行線;若公司 repo 範本改名要同步這裡。
	const char *DEFAULT_BRANCH = "main";
	const char *LAST_REPO_KEY  = "pdm/last-repo";

	QString stemOf(const QString &rel_path)
	{
		return QFileInfo(rel_path).completeBaseName();
	}

	QString workBranchOf(const QString &rel_path)
	{
		// 圖號即檔名;分支名不能有空白
		QString stem = stemOf(rel_path);
		stem.replace(' ', '-');
		return QStringLiteral("work/") + stem;
	}
}

PdmDockWidget::PdmDockWidget(QWidget *parent) :
	QDockWidget(tr("圖檔管理"), parent),
	m_service(new PdmService(this)),
	m_git(new PdmGitWorker(this))
{
	setObjectName("pdm_dock_widget");
	setUpWidget();

	connect(m_git, &PdmGitWorker::stepStarted, this,
		[this](const QString &description) {
			m_status_label->setText(description);
		});
	connect(m_git, &PdmGitWorker::allFinished, this,
		[this]() { showBusy(false); });

	// 面板首次顯示才連線,避免程式一啟動就打伺服器
	connect(this, &QDockWidget::visibilityChanged, this, [this](bool visible) {
		if (visible && !m_auto_refreshed) {
			m_auto_refreshed = true;
			refresh();
		}
	});
}

void PdmDockWidget::setUpWidget()
{
	auto *content = new QWidget(this);
	auto *layout = new QVBoxLayout(content);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(4);

	auto *top_row = new QHBoxLayout();
	m_account_label = new QLabel(tr("尚未連線"), content);
	m_refresh_button = new QPushButton(tr("重新整理"), content);
	top_row->addWidget(m_account_label, 1);
	top_row->addWidget(m_refresh_button);
	layout->addLayout(top_row);

	m_repo_combo = new QComboBox(content);
	layout->addWidget(m_repo_combo);

	m_tree = new QTreeWidget(content);
	m_tree->setHeaderLabels({tr("圖檔"), tr("狀態"), tr("鎖定者")});
	m_tree->setRootIsDecorated(false);
	m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
	m_tree->setAllColumnsShowFocus(true);
	layout->addWidget(m_tree, 1);

	auto *button_row = new QHBoxLayout();
	m_checkout_button = new QPushButton(tr("出庫並開啟"), content);
	m_checkin_button = new QPushButton(tr("入庫…"), content);
	m_cancel_button = new QPushButton(tr("取消出庫"), content);
	button_row->addWidget(m_checkout_button);
	button_row->addWidget(m_checkin_button);
	button_row->addWidget(m_cancel_button);
	layout->addLayout(button_row);

	// 進度顯示沿用元件庫「更新公司庫」模式:按鈕下方內嵌,不跳對話框
	m_status_label = new QLabel(content);
	m_status_label->setWordWrap(true);
	layout->addWidget(m_status_label);
	m_progress = new QProgressBar(content);
	m_progress->setTextVisible(false);
	m_progress->hide();
	layout->addWidget(m_progress);

	setWidget(content);

	connect(m_refresh_button, &QPushButton::clicked,
		this, &PdmDockWidget::refresh);
	connect(m_repo_combo, &QComboBox::currentIndexChanged, this,
		[this](int index) {
			if (index < 0) return;
			QSettings().setValue(LAST_REPO_KEY, currentRepoFullName());
			syncRepository();
		});
	connect(m_tree, &QTreeWidget::itemSelectionChanged,
		this, &PdmDockWidget::updateButtons);
	connect(m_checkout_button, &QPushButton::clicked,
		this, &PdmDockWidget::checkOut);
	connect(m_checkin_button, &QPushButton::clicked,
		this, &PdmDockWidget::checkIn);
	connect(m_cancel_button, &QPushButton::clicked,
		this, &PdmDockWidget::cancelCheckOut);

	updateButtons();
}

void PdmDockWidget::refresh()
{
	if (PdmSettings::token().isEmpty()) {
		m_account_label->setText(
			tr("尚未設定:請至偏好設定→圖檔管理填入伺服器與 token"));
		m_repo_combo->clear();
		m_tree->clear();
		m_files.clear();
		updateButtons();
		return;
	}
	m_account_label->setText(tr("連線中…"));
	m_service->verifyConnection([this](bool ok, const QString &login_or_error) {
		if (!ok) {
			m_account_label->setText(tr("連線失敗:%1").arg(login_or_error));
			return;
		}
		m_username = login_or_error;
		PdmSettings::setUsername(m_username);
		m_account_label->setText(tr("帳號:%1").arg(m_username));
		connectionRefreshed();
	});
}

void PdmDockWidget::connectionRefreshed()
{
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

void PdmDockWidget::syncRepository()
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

void PdmDockWidget::loadFileStates()
{
	const QString vault = vaultDir();
	m_files.clear();

	m_git->enqueue({"ls-files", "--", "*.qet"}, vault,
		[this](const PdmGitWorker::Result &result) {
			const QStringList lines = result.output.split('\n',
				Qt::SkipEmptyParts);
			for (const QString &line : lines) {
				FileState state;
				state.rel_path = line.trimmed();
				m_files.insert(state.rel_path, state);
			}
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
			rebuildTree();
		});
}

void PdmDockWidget::rebuildTree()
{
	const QString selected = m_tree->currentItem()
		? m_tree->currentItem()->text(0) : QString();
	m_tree->clear();

	QStringList paths = m_files.keys();
	paths.sort();
	for (const QString &path : paths) {
		const FileState &state = m_files.value(path);
		QString status, owner = state.lock_owner;
		if (owner == m_username) {
			status = tr("編輯中(我)");
		} else if (!owner.isEmpty()) {
			status = tr("出庫中");
		} else if (state.has_work_branch) {
			status = tr("已入庫未發行");
		} else {
			status = tr("可出庫");
		}
		auto *item = new QTreeWidgetItem(m_tree, {path, status, owner});
		if (path == selected) m_tree->setCurrentItem(item);
	}
	m_status_label->setText(tr("共 %1 個圖檔").arg(m_files.size()));
	updateButtons();
}

void PdmDockWidget::updateButtons()
{
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) {
		m_checkout_button->setEnabled(false);
		m_checkin_button->setEnabled(false);
		m_cancel_button->setEnabled(false);
		return;
	}
	const FileState state = m_files.value(item->text(0));
	const bool mine = (!m_username.isEmpty()
			   && state.lock_owner == m_username);
	m_checkout_button->setEnabled(state.lock_owner.isEmpty());
	m_checkin_button->setEnabled(mine);
	m_cancel_button->setEnabled(mine);
}

void PdmDockWidget::checkOut()
{
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->text(0);
	const FileState state = m_files.value(rel_path);
	const QString vault = vaultDir();
	const QString branch = workBranchOf(rel_path);
	const QString worktree = worktreeDir(stemOf(rel_path));

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

void PdmDockWidget::checkIn()
{
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->text(0);
	const QString branch = workBranchOf(rel_path);
	const QString worktree = worktreeDir(stemOf(rel_path));
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

	showBusy(true);
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
				[this, rel_path, vault]
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
						[this](const PdmGitWorker::Result &r) {
							if (!r.ok) {
								fail(tr("解除鎖定失敗"),
								     r.output);
							}
							refresh();
						});
				});
		});
}

void PdmDockWidget::cancelCheckOut()
{
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->text(0);
	const QString worktree = worktreeDir(stemOf(rel_path));

	const auto answer = QMessageBox::question(this, tr("取消出庫"),
		tr("將捨棄「%1」出庫後的所有未入庫修改並解除鎖定,確定?")
			.arg(rel_path));
	if (answer != QMessageBox::Yes) return;

	showBusy(true);
	if (QDir(worktree).exists()) {
		m_git->enqueue({"checkout", "--", "."}, worktree, {});
	}
	m_git->enqueue({"lfs", "unlock", rel_path}, vaultDir(),
		[this](const PdmGitWorker::Result &result) {
			if (!result.ok) fail(tr("解除鎖定失敗"), result.output);
			refresh();
		});
}

void PdmDockWidget::showBusy(bool busy)
{
	if (busy) {
		m_progress->setRange(0, 0);
		m_progress->show();
	} else {
		m_progress->hide();
	}
	m_refresh_button->setEnabled(!busy);
	m_repo_combo->setEnabled(!busy);
}

void PdmDockWidget::fail(const QString &title, const QString &log)
{
	showBusy(false);
	m_status_label->setText(title);
	QMessageBox::warning(this, title, log.right(1500));
}

QString PdmDockWidget::currentRepoFullName() const
{
	return m_repo_combo->currentText();
}

QString PdmDockWidget::vaultDir() const
{
	return PdmSettings::workRoot() + '/' + currentRepoFullName()
		+ QStringLiteral("/vault");
}

QString PdmDockWidget::worktreeDir(const QString &stem) const
{
	return PdmSettings::workRoot() + '/' + currentRepoFullName()
		+ QStringLiteral("/checkouts/") + stem;
}

QString PdmDockWidget::remoteUrlWithCredentials() const
{
	// TODO:token 會留在 vault 的 .git/config;內網暫可接受,
	// 之後改 git credential helper(見開發計畫注意事項 8)。
	QUrl url(PdmSettings::serverUrl());
	url.setUserName(m_username);
	url.setPassword(PdmSettings::token());
	url.setPath('/' + currentRepoFullName() + QStringLiteral(".git"));
	return url.toString(QUrl::FullyEncoded);
}

QTreeWidgetItem *PdmDockWidget::selectedFileItem() const
{
	return m_tree->currentItem();
}
