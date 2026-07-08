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

#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <memory>

namespace
{
	// Phase 1 固定用 main 當發行線;若公司 repo 範本改名要同步這裡。
	const char *DEFAULT_BRANCH = "main";
	const char *LAST_REPO_KEY  = "pdm/last-repo";

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
	resize(560, 520);
	setUpWidget();

	connect(m_git, &PdmGitWorker::stepStarted, this,
		[this](const QString &description) {
			m_status_label->setText(description);
		});
	connect(m_git, &PdmGitWorker::allFinished, this,
		[this]() { showBusy(false); });
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

	// 三排動作鈕:依選取圖檔的狀態顯示/隱藏
	auto *edit_row = new QHBoxLayout();
	m_checkout_button = new QPushButton(tr("出庫並開啟"), content);
	m_checkin_button = new QPushButton(tr("入庫…"), content);
	m_cancel_button = new QPushButton(tr("取消出庫"), content);
	edit_row->addWidget(m_checkout_button);
	edit_row->addWidget(m_checkin_button);
	edit_row->addWidget(m_cancel_button);
	layout->addLayout(edit_row);

	auto *review_row = new QHBoxLayout();
	m_submit_button = new QPushButton(tr("送審…"), content);
	m_review_button = new QPushButton(tr("審核檢視(唯讀)"), content);
	m_approve_button = new QPushButton(tr("核准"), content);
	m_reject_button = new QPushButton(tr("退回…"), content);
	review_row->addWidget(m_submit_button);
	review_row->addWidget(m_review_button);
	review_row->addWidget(m_approve_button);
	review_row->addWidget(m_reject_button);
	layout->addLayout(review_row);

	auto *release_row = new QHBoxLayout();
	m_release_button = new QPushButton(tr("發行…"), content);
	m_force_unlock_button = new QPushButton(tr("強制解鎖…"), content);
	m_history_button = new QPushButton(tr("發行歷史…"), content);
	release_row->addWidget(m_release_button);
	release_row->addWidget(m_force_unlock_button);
	release_row->addWidget(m_history_button);
	layout->addLayout(release_row);

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
	connect(m_repo_combo, &QComboBox::currentIndexChanged, this,
		[this](int index) {
			if (index < 0) return;
			QSettings().setValue(LAST_REPO_KEY, currentRepoFullName());
			syncRepository();
		});
	connect(m_tree, &QTreeWidget::itemSelectionChanged,
		this, &PdmDialog::updateButtons);
	connect(m_checkout_button, &QPushButton::clicked,
		this, &PdmDialog::checkOut);
	connect(m_checkin_button, &QPushButton::clicked,
		this, &PdmDialog::checkIn);
	connect(m_cancel_button, &QPushButton::clicked,
		this, &PdmDialog::cancelCheckOut);
	connect(m_submit_button, &QPushButton::clicked,
		this, &PdmDialog::submitForReview);
	connect(m_review_button, &QPushButton::clicked,
		this, &PdmDialog::openReviewView);
	connect(m_approve_button, &QPushButton::clicked,
		this, &PdmDialog::approve);
	connect(m_reject_button, &QPushButton::clicked,
		this, &PdmDialog::rejectReview);
	connect(m_release_button, &QPushButton::clicked,
		this, &PdmDialog::releaseApproved);
	connect(m_force_unlock_button, &QPushButton::clicked,
		this, &PdmDialog::forceUnlock);
	connect(m_history_button, &QPushButton::clicked,
		this, &PdmDialog::showReleaseHistory);

	updateButtons();
}

void PdmDialog::refresh()
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
			loadPullRequests();
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

void PdmDialog::rebuildTree()
{
	const QString selected = m_tree->currentItem()
		? m_tree->currentItem()->text(0) : QString();
	m_tree->clear();

	QStringList paths = m_files.keys();
	paths.sort();
	for (const QString &path : paths) {
		const FileState &state = m_files.value(path);
		QString status;
		const QString owner = state.lock_owner;
		if (state.pr_index > 0) {
			status = state.pr_approved
				? tr("已確認待發行(#%1)").arg(state.pr_index)
				: tr("審核中(#%1)").arg(state.pr_index);
		} else if (owner == m_username && !owner.isEmpty()) {
			status = tr("編輯中(我)");
		} else if (!owner.isEmpty()) {
			status = tr("出庫中");
		} else if (state.has_work_branch) {
			status = tr("已入庫未送審");
		} else {
			status = tr("可出庫");
		}
		auto *item = new QTreeWidgetItem(m_tree, {path, status, owner});
		if (path == selected) m_tree->setCurrentItem(item);
	}
	m_status_label->setText(tr("共 %1 個圖檔").arg(m_files.size()));
	updateButtons();
}

void PdmDialog::updateButtons()
{
	const QTreeWidgetItem *item = selectedFileItem();
	const FileState state = item ? m_files.value(item->text(0)) : FileState();
	const bool has_selection = (item != nullptr);
	const bool locked_by_me = has_selection && !m_username.isEmpty()
				  && state.lock_owner == m_username;
	const bool locked_by_other = has_selection && !state.lock_owner.isEmpty()
				     && !locked_by_me;
	const bool in_review = has_selection && state.pr_index > 0;

	// 出庫:未鎖定且不在審核流程中(送審後圖對製圖者唯讀)
	m_checkout_button->setEnabled(has_selection && state.lock_owner.isEmpty()
				      && !in_review);
	m_checkin_button->setEnabled(locked_by_me && !in_review);
	m_cancel_button->setEnabled(locked_by_me);

	// 送審:已入庫(work 分支存在)、未鎖定、尚無 PR
	m_submit_button->setEnabled(has_selection && state.has_work_branch
				    && state.lock_owner.isEmpty() && !in_review);
	// 審核動作:有 PR 即可檢視;核准/退回限非作者(伺服器也會擋)
	m_review_button->setEnabled(in_review);
	m_approve_button->setEnabled(in_review && state.pr_author != m_username);
	m_reject_button->setEnabled(in_review && state.pr_author != m_username);

	// 發行:PR 已有有效核准(是否有權限由伺服器判定)
	m_release_button->setEnabled(in_review && state.pr_approved);
	m_force_unlock_button->setVisible(locked_by_other);
	m_force_unlock_button->setEnabled(locked_by_other);
	m_history_button->setEnabled(has_selection);
}

void PdmDialog::checkOut()
{
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->text(0);
	const FileState state = m_files.value(rel_path);
	const QString vault = vaultDir();
	const QString branch = workBranchOf(rel_path);
	const QString worktree = worktreeDir(sanitizedStem(rel_path));

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
	const QString rel_path = item->text(0);
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
							refresh();
						});
				});
		});
}

void PdmDialog::cancelCheckOut()
{
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->text(0);
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
			refresh();
		});
}

void PdmDialog::submitForReview()
{
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->text(0);
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
	m_service->createPullRequest(currentRepoFullName(),
		workBranchOf(rel_path), DEFAULT_BRANCH,
		tr("%1 送審").arg(stem), body.trimmed(),
		[this, rel_path](const PdmService::Reply &reply) {
			showBusy(false);
			if (!reply.ok) {
				fail(tr("送審失敗"), reply.error);
				return;
			}
			const int pr = reply.json.object()
				.value(QStringLiteral("number")).toInt();
			m_status_label->setText(
				tr("「%1」已送審(#%2)").arg(rel_path).arg(pr));
			refresh();
		});
}

void PdmDialog::openReviewView()
{
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->text(0);
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

void PdmDialog::approve()
{
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->text(0);
	const FileState state = m_files.value(rel_path);
	if (state.pr_index <= 0) return;

	const auto answer = QMessageBox::question(this, tr("核准"),
		tr("確認核准「%1」(#%2)?\n請先以「審核檢視」檢視過圖面。")
			.arg(rel_path).arg(state.pr_index));
	if (answer != QMessageBox::Yes) return;

	showBusy(true);
	// 核准前先確認 PR head 沒有在審核期間被更新過
	m_service->get(QStringLiteral("/repos/%1/pulls/%2")
		.arg(currentRepoFullName()).arg(state.pr_index),
		[this, rel_path, state](const PdmService::Reply &reply) {
			const QString current_sha = reply.json.object()
				.value(QStringLiteral("head")).toObject()
				.value(QStringLiteral("sha")).toString();
			if (!reply.ok || current_sha != state.pr_head_sha) {
				showBusy(false);
				QMessageBox::warning(this, tr("核准"),
					tr("送審內容已被更新,清單已重新整理,"
					   "請重新開啟審核檢視後再核准。"));
				refresh();
				return;
			}
			m_service->submitReview(currentRepoFullName(),
				state.pr_index, QStringLiteral("APPROVED"),
				tr("圖面確認無誤(QET 審核模式)"),
				[this, rel_path](const PdmService::Reply &r) {
					showBusy(false);
					if (!r.ok) {
						fail(tr("核准失敗"), r.error);
						return;
					}
					m_status_label->setText(
						tr("「%1」已核准").arg(rel_path));
					refresh();
				});
		});
}

void PdmDialog::rejectReview()
{
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->text(0);
	const FileState state = m_files.value(rel_path);
	if (state.pr_index <= 0) return;

	bool accepted = false;
	const QString reason = QInputDialog::getMultiLineText(this,
		tr("退回"), tr("退回意見(必填,製圖者會看到):"),
		QString(), &accepted);
	if (!accepted) return;
	if (reason.trimmed().isEmpty()) {
		QMessageBox::warning(this, tr("退回"), tr("退回意見不可空白。"));
		return;
	}

	showBusy(true);
	m_service->submitReview(currentRepoFullName(), state.pr_index,
		QStringLiteral("REQUEST_CHANGES"), reason.trimmed(),
		[this, rel_path](const PdmService::Reply &reply) {
			showBusy(false);
			if (!reply.ok) {
				fail(tr("退回失敗"), reply.error);
				return;
			}
			m_status_label->setText(tr("「%1」已退回").arg(rel_path));
			refresh();
		});
}

void PdmDialog::releaseApproved()
{
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->text(0);
	const FileState state = m_files.value(rel_path);
	if (state.pr_index <= 0 || !state.pr_approved) return;
	const QString stem = sanitizedStem(rel_path);
	const QString vault = vaultDir();

	showBusy(true);
	// 1. 由既有 tag 推下一版次
	m_git->enqueue({"ls-remote", "--tags", "origin",
		QStringLiteral("refs/tags/release/%1-v*").arg(stem)}, vault,
		[this, rel_path, state, stem, vault]
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

			const auto answer = QMessageBox::question(this, tr("發行"),
				tr("將「%1」(#%2)合入 %3 並發行為 %4,確定?")
					.arg(rel_path).arg(state.pr_index)
					.arg(QLatin1String(DEFAULT_BRANCH), tag));
			if (answer != QMessageBox::Yes) {
				showBusy(false);
				return;
			}

			// 2. merge(核准後可合併狀態有延遲,service 內建重試)
			m_service->mergePullRequest(currentRepoFullName(),
				state.pr_index,
				[this, rel_path, stem, tag, vault]
				(const PdmService::Reply &merge_reply) {
				if (!merge_reply.ok) {
					fail(tr("合併失敗(需有效核准,且僅"
						"放行者有權發行)"),
					     merge_reply.error);
					return;
				}
				// 3. 更新 vault 到合併後的 main
				m_git->enqueue({"fetch", "origin", "--prune"},
					vault, {});
				m_git->enqueue({"checkout", DEFAULT_BRANCH},
					vault, {});
				m_git->enqueue({"pull", "--ff-only"}, vault, {});
				m_git->enqueue({"rev-parse", "HEAD"}, vault,
					[this, rel_path, stem, tag, vault]
					(const PdmGitWorker::Result &sha_result) {
					const QString sha =
						sha_result.output.trimmed();
					finishRelease(rel_path, stem, tag, sha);
				});
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
	const QString rel_path = item->text(0);
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

void PdmDialog::showReleaseHistory()
{
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->text(0);
	const QString stem = sanitizedStem(rel_path);

	m_service->get(QStringLiteral("/repos/%1/releases?limit=50")
		.arg(currentRepoFullName()),
		[this, rel_path, stem](const PdmService::Reply &reply) {
		if (!reply.ok) {
			fail(tr("讀取發行歷史失敗"), reply.error);
			return;
		}
		QStringList tags;
		const QJsonArray releases = reply.json.array();
		const QString prefix = QStringLiteral("release/%1-v").arg(stem);
		for (const QJsonValue &value : releases) {
			const QString tag = value.toObject()
				.value(QStringLiteral("tag_name")).toString();
			if (tag.startsWith(prefix)) tags << tag;
		}
		if (tags.isEmpty()) {
			QMessageBox::information(this, tr("發行歷史"),
				tr("「%1」尚無發行版。").arg(rel_path));
			return;
		}
		bool accepted = false;
		const QString chosen = QInputDialog::getItem(this,
			tr("發行歷史"),
			tr("「%1」的發行版(選擇後以唯讀開啟):").arg(rel_path),
			tags, 0, false, &accepted);
		if (!accepted || chosen.isEmpty()) return;

		// 發行版以 detached worktree 唯讀開啟(lockable 保唯讀)
		const QString vault = vaultDir();
		QString dir_name = chosen;
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
			// 發行版檢視強制唯讀
			setFileWritable(view_dir + '/' + rel_path, false);
			emit requestOpenFile(view_dir + '/' + rel_path);
			showBusy(false);
		};
		if (QDir(view_dir).exists()) {
			open_view({true, 0, QString()});
		} else {
			m_git->enqueue({"worktree", "add", "--detach",
				view_dir, chosen}, vault, open_view);
		}
	});
}

void PdmDialog::showBusy(bool busy)
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

void PdmDialog::fail(const QString &title, const QString &log)
{
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
	return item ? m_files.value(item->text(0)) : FileState();
}
