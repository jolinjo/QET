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
#include "pdmreleasehistory.h"
#include "pdmrevision.h"
#include "pdmservice.h"
#include "pdmsettings.h"
#include "pdmversion.h"

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
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QGroupBox>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTimer>
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

	// 文件狀態→底色(隨狀態變化)。以關鍵字比對,涵蓋即時流程狀態與
	// 檔內 doc-status 雙語值。順序有意義:「待發行」需先於「發行」。
	QColor statusColor(const QString &s)
	{
		if (s.contains(QStringLiteral("待發行")))
			return QColor(0xB2, 0xDF, 0xDB);   // 青:已確認待發行
		if (s.contains(QStringLiteral("審核")))
			return QColor(0xBB, 0xDE, 0xFB);   // 藍:審核中
		if (s.contains(QStringLiteral("發行"))
		    || s.contains(QStringLiteral("Released")))
			return QColor(0xC8, 0xE6, 0xC9);   // 綠:正式發行
		if (s.contains(QStringLiteral("編輯")))
			return QColor(0xFF, 0xE0, 0xB2);   // 橙:編輯中
		if (s.contains(QStringLiteral("出庫中")))
			return QColor(0xFF, 0xCC, 0xBC);   // 深橙:他人出庫中
		if (s.contains(QStringLiteral("未送審")))
			return QColor(0xD1, 0xC4, 0xE9);   // 紫:已入庫未送審
		if (s.contains(QStringLiteral("可出庫")))
			return QColor(0xEC, 0xEF, 0xF1);   // 灰:閒置可出庫
		return QColor();
	}

	// 只替「狀態」欄上底色,且底色不被整列選取色蓋掉(選取時仍看得到狀態色)。
	class StatusBgDelegate : public QStyledItemDelegate
	{
		public:
			using QStyledItemDelegate::QStyledItemDelegate;
			void paint(QPainter *painter,
				   const QStyleOptionViewItem &option,
				   const QModelIndex &index) const override
			{
				const QColor c = statusColor(index.data().toString());
				QStyleOptionViewItem opt(option);
				if (c.isValid()) {
					painter->fillRect(option.rect, c);
					// 蓋掉選取底色,改用深色文字確保可讀
					opt.state &= ~QStyle::State_Selected;
					opt.palette.setColor(QPalette::Text, Qt::black);
				}
				QStyledItemDelegate::paint(painter, opt, index);
			}
	};

	// 把 git / Gitea 的英文錯誤訊息翻成使用者看得懂的中文。
	// 只有「認得的」錯誤才轉譯並隱藏原文;認不得的一律附上原文供通報。
	// 回傳空字串代表「無對應」——由呼叫端決定是否顯示原文。
	QString translateError(const QString &raw)
	{
		const QString s = raw.toLower();
		auto has = [&s](const char *needle) {
			return s.contains(QLatin1String(needle));
		};

		// Windows Gitea 端 git 子程序/hook 起不來(DLL 初始化失敗)。
		if (has("0xc0000142"))
			return QObject::tr(
				"圖庫伺服器暫時無法處理這次請求(伺服器端 git 程序"
				"啟動失敗)。通常是伺服器忙碌或需重新啟動,請稍候再試;"
				"若持續發生,請通知管理員重啟 Gitea 服務。");
		if (has("exit status 0xc") || has("exit status 3221"))
			return QObject::tr(
				"圖庫伺服器端 git 程序異常結束,多為一時性,請稍候再試;"
				"若持續發生請通知管理員。");

		// 連線類
		if (has("could not resolve host") || has("failed to connect")
		    || has("couldn't connect") || has("connection refused")
		    || has("unable to access") || has("timed out")
		    || has("connection timed") || has("no route to host"))
			return QObject::tr(
				"無法連線圖庫伺服器,請確認網路 / VPN 是否正常、"
				"伺服器是否開啟。");

		// 認證 / 權限
		if (has("authentication failed") || has("http 401")
		    || has("401 unauthorized"))
			return QObject::tr(
				"認證失敗:token 無效或已過期,請至偏好設定→圖檔管理"
				"更新 token。");
		if (has("http 403") || has("forbidden"))
			return QObject::tr("權限不足:你沒有執行此操作的權限。");
		if (has("http 404") || has("404 not found"))
			return QObject::tr(
				"找不到對應資料(可能已被刪除或改名),請按重新整理。");
		if (has("http 409") || has("conflict"))
			return QObject::tr(
				"狀態衝突:資料已被他人變更,請先重新整理再操作。");
		if (has("http 422"))
			return QObject::tr(
				"伺服器拒絕此操作(單人測試時常見於審核/核准自己"
				"送出的項目)。");

		// git push / 落後遠端
		if (has("non-fast-forward") || has("fetch first")
		    || has("[rejected]") || has("tip of your current branch"))
			return QObject::tr(
				"遠端圖庫已更新,請先按重新整理再操作。");
		if (has("index.lock"))
			return QObject::tr(
				"本機 git 鎖檔殘留(系統會嘗試自動清除重試);"
				"若仍失敗請關閉圖檔管理視窗重開。");
		if (has("merge") && has("conflict"))
			return QObject::tr(
				"合併衝突:此圖與最新版有衝突,需人工處理。");

		// 伺服器 5xx(放在 0xc0000142 之後,當作一般性後備)
		if (has("http 500") || has("returned error: 500") || has("error: 500")
		    || has("http 502") || has("http 503") || has("http 504")
		    || has("502 bad gateway") || has("503 service"))
			return QObject::tr(
				"圖庫伺服器暫時無法處理(HTTP 5xx),多為一時性,"
				"請稍候再試;若持續發生請通知管理員查看伺服器。");

		return QString();   // 認不得 → 由呼叫端顯示原文
	}

	// git-lfs unlock 對「本來就沒鎖」會回「no matching locks / unable to get
	// lock ID」。對我們而言鎖已不存在＝已達成解鎖目的,視為成功不報錯
	// (常見於前一步入庫送審已解鎖、但後續建 PR 失敗後又再按入庫)。
	bool lfsUnlockBenign(const QString &out)
	{
		const QString s = out.toLower();
		return s.contains(QLatin1String("no matching locks"))
		    || s.contains(QLatin1String("unable to get lock id"))
		    || s.contains(QLatin1String("no locks found"));
	}

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

	// 進度對話框由 git 活動驅動,收框用防抖:任何 git 步驟一開始就顯示同一個
	// 框並更新說明;佇列清空後延遲 400ms 才收——期間若又有步驟(多階段/夾帶
	// REST 的操作、或操作後的背景重整),會取消收框,所以整段只有「一個框
	// 一直開著」,不會彈一堆、也看得出何時真的完成。
	m_hide_timer = new QTimer(this);
	m_hide_timer->setSingleShot(true);
	m_hide_timer->setInterval(400);
	connect(m_hide_timer, &QTimer::timeout, this, [this]() {
		if (m_busy_dialog) m_busy_dialog->hide();
		m_refresh_button->setEnabled(true);
		m_repo_combo->setEnabled(true);
	});
	connect(m_git, &PdmGitWorker::stepStarted, this,
		[this](const QString &cmd) {
			const QString text = friendlyStep(cmd);
			m_status_label->setText(text);
			ensureBusyDialog();
			m_busy_label->setText(text);
			m_hide_timer->stop();
			if (!m_busy_dialog->isVisible()) {
				m_busy_dialog->show();
				m_busy_dialog->raise();
				m_busy_bar->setValue(0);   // 背景讀取自行彈框:從頭開始
			}
			// 一個操作+它引發的重新查詢視為「同一段等待」:每個 git 步驟
			// (不分操作或背景重整)都往上補 1/4 剩餘距離,連續漸進逼近
			// 95%,不倒退;全部真的做完(allFinished)才補滿 100% 再收框。
			const int v = m_busy_bar->value();
			if (v < 95) m_busy_bar->setValue(v + (95 - v) / 4);
			m_refresh_button->setEnabled(false);
			m_repo_combo->setEnabled(false);
		});
	connect(m_git, &PdmGitWorker::allFinished, this, [this]() {
		m_op_active = false;
		m_op_total = 0;
		if (m_busy_bar) m_busy_bar->setValue(100);   // 完成:補滿再收框
		m_hide_timer->start();   // 防抖收框(有新步驟會取消)
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
	m_add_button = new QPushButton(tr("新檔入庫…"), content);   // 放到「繪製」群組
	m_view_released_button = new QPushButton(tr("不出庫檢視"), content);
	m_refresh_button = new QPushButton(tr("重新整理"), content);
	top_row->addWidget(m_account_label, 1);
	m_view_released_button->setMinimumWidth(160);
	m_view_released_button->setMinimumHeight(32);
	top_row->addWidget(m_view_released_button);
	top_row->addWidget(m_refresh_button);
	layout->addLayout(top_row);

	// 狀態文字放帳號下方(檔案數/結果訊息)。進度一律用彈出進度框顯示,
	// 視窗內不再另放進度條,統一一種進度呈現。
	m_status_label = new QLabel(content);
	m_status_label->setWordWrap(true);
	layout->addWidget(m_status_label);

	m_repo_combo = new QComboBox(content);
	layout->addWidget(m_repo_combo);

	// 三欄:資料夾樹 | 圖檔清單 | 所選圖檔的發行歷史
	auto *splitter = new QSplitter(Qt::Horizontal, content);

	m_folder_tree = new QTreeWidget(splitter);
	m_folder_tree->setHeaderLabels({tr("資料夾")});
	m_folder_tree->setSelectionMode(QAbstractItemView::SingleSelection);

	m_tree = new QTreeWidget(splitter);
	m_tree->setHeaderLabels({tr("版本"), tr("檔名"), tr("狀態"),
				 tr("繪製者"), tr("確認者"), tr("核准者")});
	m_tree->setRootIsDecorated(false);
	m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
	m_tree->setAllColumnsShowFocus(true);
	// 「狀態」欄(index 2)上隨狀態變化的底色
	m_tree->setItemDelegateForColumn(2, new StatusBgDelegate(m_tree));
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

	// 列高加大約 1.5 倍(上下各補 ~1/4 字高的 padding),好點選、好閱讀。
	// 只加 padding、不設背景,保留發行版列的底色 highlight。
	const int vpad = fontMetrics().height() / 4;
	// 左右各補約一個中文字寬,欄與欄之間至少留兩個字的間隔,字不擠在一起。
	const int hpad = fontMetrics().horizontalAdvance(QChar(0x4e2d));
	const QString row_ss = QStringLiteral(
		"QTreeView::item { padding-top: %1px; padding-bottom: %1px;"
		" padding-left: %2px; padding-right: %2px; }")
		.arg(vpad).arg(hpad);
	m_folder_tree->setStyleSheet(row_ss);
	m_tree->setStyleSheet(row_ss);
	m_history_tree->setStyleSheet(row_ss);

	splitter->addWidget(m_folder_tree);
	splitter->addWidget(m_tree);
	splitter->addWidget(m_history_tree);
	splitter->setStretchFactor(0, 1);
	splitter->setStretchFactor(1, 3);
	splitter->setStretchFactor(2, 3);
	layout->addWidget(splitter, 1);

	// 動作區依三個角色/階段分組:繪製 → 確認 → 核准 三欄並排。每欄用有標題的
	// QGroupBox 視覺區隔,按鈕依選取圖檔狀態 enable/disable。「審核檢視」「退回」
	// 兩個動作在「確認」「核准」兩組都要,故各放一顆(呼叫同一功能)。
	auto make_stage = [content](const QString &title,
				    const QList<QPushButton *> &btns) {
		auto *box = new QGroupBox(title, content);
		auto *col = new QVBoxLayout(box);
		for (QPushButton *b : btns) col->addWidget(b);
		col->addStretch();
		return box;
	};

	// 繪製（新檔入庫、出入庫、送審)。「不出庫檢視」移到上方與重新整理同列。
	m_checkout_button = new QPushButton(tr("出庫開啟"), content);
	m_checkin_button = new QPushButton(tr("入庫納管…"), content);
	m_cancel_button = new QPushButton(tr("取消出庫"), content);
	m_submit_direct_button = new QPushButton(tr("入庫送審…"), content);
	m_submit_button = new QPushButton(tr("完成送審…"), content);
	m_force_unlock_button = new QPushButton(tr("強制解鎖…"), content);
	// 確認
	m_review_button = new QPushButton(tr("審核檢視"), content);
	m_approve_button = new QPushButton(tr("確認完成…"), content);
	m_reject_button = new QPushButton(tr("退回…"), content);
	// 核准
	m_review_button2 = new QPushButton(tr("審核檢視"), content);
	m_release_button = new QPushButton(tr("核准發行…"), content);
	m_reject_button2 = new QPushButton(tr("退回…"), content);
	m_revert_button = new QPushButton(tr("退回上一發行版…"), content);

	auto *stages = new QHBoxLayout();
	stages->addWidget(make_stage(tr("繪製"),
		{m_add_button, m_checkout_button,
		 m_checkin_button, m_cancel_button, m_submit_direct_button,
		 m_submit_button, m_force_unlock_button}), 1);
	stages->addWidget(make_stage(tr("確認"),
		{m_review_button, m_approve_button, m_reject_button}), 1);
	stages->addWidget(make_stage(tr("核准"),
		{m_review_button2, m_release_button, m_reject_button2,
		 m_revert_button}), 1);
	layout->addLayout(stages);

	// 管理員維護（僅核准者可見,結構性變更直接改 main）
	m_admin_box = new QGroupBox(tr("管理員維護"), content);
	auto *admin_row = new QHBoxLayout(m_admin_box);
	m_add_folder_button = new QPushButton(tr("新增資料夾…"), content);
	m_del_folder_button = new QPushButton(tr("刪除資料夾…"), content);
	m_del_drawing_button = new QPushButton(tr("刪除圖檔…"), content);
	admin_row->addWidget(m_add_folder_button);
	admin_row->addWidget(m_del_folder_button);
	admin_row->addWidget(m_del_drawing_button);
	layout->addWidget(m_admin_box);

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
	connect(m_submit_direct_button, &QPushButton::clicked,
		this, &PdmDialog::checkInAndSubmit);
	connect(m_submit_button, &QPushButton::clicked,
		this, &PdmDialog::submitForReview);
	connect(m_review_button, &QPushButton::clicked,
		this, &PdmDialog::openReviewView);
	connect(m_review_button2, &QPushButton::clicked,
		this, &PdmDialog::openReviewView);
	connect(m_approve_button, &QPushButton::clicked,
		this, &PdmDialog::confirmDone);
	connect(m_reject_button, &QPushButton::clicked,
		this, &PdmDialog::rejectReview);
	connect(m_reject_button2, &QPushButton::clicked,
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
	// 重整是「操作結束後的背景讀取」邊界:先收起進度對話框,之後的
	// 背景 git(讀清單/歷史)就不會再彈框。
	showBusy(false);
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
		m_account_label->setText(tr("操作者:%1").arg(m_username));
		// 取 email 供比對 work 分支作者(判斷「繪製者本人」)
		m_service->get(QStringLiteral("/user"),
			[this](const PdmService::Reply &reply) {
				if (reply.ok)
					m_user_email = reply.json.object()
						.value(QStringLiteral("email")).toString();
			});
		// 角色(確認者/核准者)依「本 repo 所屬 org」判定,必須等 repo 確定
		// 後才抓——放在 syncRepository()。這裡先抓會因 combo 尚未載入而拿到
		// 空 org,導致第一次進來角色全為 false(管理員維護不出現)。
		connectionRefreshed();
	});
}

void PdmDialog::loadUserRoles(int retries_left)
{
	m_is_confirmer = false;
	m_is_releaser = false;
	// 本 repo 所屬組織(owner)底下的 team 才算數
	const QString org = currentRepoFullName().section('/', 0, 0);
	m_service->get(QStringLiteral("/user/teams?limit=50"),
		[this, org, retries_left](const PdmService::Reply &reply) {
			// 暫時性失敗(網路抖動)不要讓角色永久歸零、按鈕整排消失:
			// 隔一小段時間重試,重試用盡才放行。
			if (!reply.ok) {
				if (retries_left > 0)
					QTimer::singleShot(1500, this,
						[this, retries_left]() {
							loadUserRoles(retries_left - 1);
						});
				else
					updateButtons();
				return;
			}
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
	const std::function<void ()> &after_push,
	bool set_revision, const QString &revision, int progress_steps,
	const std::function<bool (const QString &)> &post_stamp)
{
	const QString stem = sanitizedStem(rel_path);
	const QString branch = workBranchOf(rel_path);
	const QString worktree = worktreeDir(stem);
	const QString vault = vaultDir();
	const QString abs_path = worktree + '/' + rel_path;

	// 戳記→add→commit→push→after_push(工作區已在 origin/branch 最新狀態)
	auto stamp_and_push = [this, rel_path, branch, worktree, abs_path,
			       status, extra_fields, commit_message, after_push,
			       set_revision, revision, post_stamp]
		(const PdmGitWorker::Result &prep) {
		if (!prep.ok) {
			fail(tr("準備簽核工作區失敗"), prep.output);
			return;
		}
		setFileWritable(abs_path, true);
		if (!stampDocFields(abs_path, status, revision, set_revision,
				    extra_fields)) {
			fail(tr("寫入圖框欄位失敗"), abs_path);
			return;
		}
		// 戳記後、commit 前的額外編輯(發行:append 發行史列)
		if (post_stamp && !post_stamp(abs_path)) {
			fail(tr("寫入發行記錄失敗"), abs_path);
			return;
		}
		// 直接 commit 指定檔案(git commit -- <file> 會把工作區該檔內容一併
		// 提交,不需獨立 add,避免 add 與 commit 之間 staging 被清掉的問題)。
		// commit 成功才 push、才繼續(確認/發行);commit 失敗(且非「無變更」)
		// 一律中止——絕不用未戳記的內容繼續合併發行(舊 bug 就是這樣把編輯中
		// 內容併進 main)。stale index.lock 由 git worker 自我修復重試。
		m_git->enqueue({"commit", "-m", commit_message, "--", rel_path},
			worktree,
			[this, branch, worktree, after_push]
			(const PdmGitWorker::Result &commit_r) {
			if (!commit_r.ok && !commit_r.output.contains(
				    QLatin1String("nothing to commit"))) {
				fail(tr("簽核 commit 失敗"), commit_r.output);
				return;
			}
			m_git->enqueue({"push", "origin", branch}, worktree,
				[this, after_push]
				(const PdmGitWorker::Result &push_result) {
					if (!push_result.ok) {
						fail(tr("推送簽核 commit 失敗"),
						     push_result.output);
						return;
					}
					after_push();
				});
		});
	};

	showBusy(true, true, progress_steps);
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
	loadUserRoles();         // repo 已確定,可正確依 org 判定確認者/核准者角色
	showBusy(true, false);   // 背景同步:只用內嵌進度條,不彈模態框

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

	// 發現 work 分支;新檔(僅存在於 work 分支、尚未合併回 main)也要列出,
	// 否則「新檔入庫」後在清單看不到自己剛出庫的檔。標記既有檔的
	// has_work_branch,並對「孤兒 work 分支」(main 上沒有對應檔)ls-tree
	// 取其 .qet 路徑補進清單。發現階段全部完成後才套用中繼資料。
	m_git->enqueue({"ls-remote", "--heads", "origin", "refs/heads/work/*"},
		vault,
		[this, vault](const PdmGitWorker::Result &result) {
			QStringList work_branches;   // work/<stem>
			const QStringList lines = result.output.split('\n',
				Qt::SkipEmptyParts);
			for (const QString &line : lines) {
				const int position = line.indexOf(
					QStringLiteral("refs/heads/"));
				if (position >= 0)
					work_branches << line.mid(position + 11).trimmed();
			}

			QSet<QString> existing_stems;
			for (auto it = m_files.constBegin();
			     it != m_files.constEnd(); ++it)
				existing_stems.insert(sanitizedStem(it.key()));

			QStringList orphan_branches;
			for (const QString &b : work_branches) {
				const QString stem = b.startsWith(QLatin1String("work/"))
					? b.mid(5) : b;
				bool matched = false;
				for (auto it = m_files.begin(); it != m_files.end(); ++it) {
					if (sanitizedStem(it.key()) == stem) {
						it->has_work_branch = true;
						matched = true;
					}
				}
				if (!matched && !existing_stems.contains(stem))
					orphan_branches << b;
			}

			if (orphan_branches.isEmpty()) {
				applyFileMetadata(vault);
				return;
			}
			// 對每個孤兒分支 ls-tree 取實際 .qet 路徑(檔名含資料夾,
			// 無法只由分支名還原,故要查樹)。
			auto remaining =
				std::make_shared<int>(orphan_branches.size());
			for (const QString &b : orphan_branches) {
				m_git->enqueue({"ls-tree", "-r", "--name-only",
					QStringLiteral("origin/") + b, "--", "*.qet"},
					vault,
					[this, vault, remaining]
					(const PdmGitWorker::Result &tree_r) {
					const QStringList paths = tree_r.output.split('\n',
						Qt::SkipEmptyParts);
					for (const QString &p : paths) {
						const QString rp = p.trimmed();
						if (rp.isEmpty() || m_files.contains(rp))
							continue;
						FileState s;
						s.rel_path = rp;
						s.has_work_branch = true;
						m_files.insert(rp, s);
					}
					if (--(*remaining) == 0) applyFileMetadata(vault);
				});
			}
		});
}

void PdmDialog::applyFileMetadata(const QString &vault)
{
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

	// 進行中的圖檔:版本/狀態/簽核者改讀 work 分支的圖框
	//(這些值尚未合併回 main,清單直接讀 main 會顯示成舊值;新檔在 main
	// 上根本不存在,更必須讀 work 分支)。
	QStringList work_paths;
	for (auto it = m_files.constBegin(); it != m_files.constEnd(); ++it)
		if (it->has_work_branch) work_paths << it.key();
	if (work_paths.isEmpty()) {
		loadPullRequests();
		return;
	}
	auto remaining = std::make_shared<int>(work_paths.size());
	for (const QString &rp : work_paths) {
		m_git->enqueue({"show", QStringLiteral("origin/")
			+ workBranchOf(rp) + ':' + rp}, vault,
			[this, rp, remaining](const PdmGitWorker::Result &show_r) {
			QDomDocument doc;
			if (doc.setContent(show_r.output)) {
				auto it = m_files.find(rp);
				if (it != m_files.end())
					parseDocFields(doc, &(*it));
			}
			if (--(*remaining) == 0) loadPullRequests();
		});
	}
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
	// 版本改讀專案級 pdm_work_version(舊檔自動退回首頁 indexrev)。
	// 見 doc/pdm-revision-design.md §2:版本不再逐頁存放。
	state->revision = PdmVersion::workVersion(doc);

	// 其餘代表欄位仍取首頁(第一個 <diagram>)的圖框資料
	const QDomElement diagram =
		doc.documentElement().firstChildElement(QStringLiteral("diagram"));
	if (diagram.isNull()) return;

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

	// 版本寫到專案級 pdm_work_version,不再逐頁蓋 indexrev——各頁 indexrev
	// 交還使用者當「頁修訂索引」。見 doc/pdm-revision-design.md §2。
	if (set_revision) {
		if (PdmVersion::setProjectProperty(doc,
			QLatin1String(PdmVersion::WORK_VERSION), revision))
			changed = true;
	}

	// 簽核附加欄位(文件狀態/確認者/核准者)仍逐頁寫,維持既有代表值語意
	for (QDomElement diagram = root.firstChildElement(
		QStringLiteral("diagram"));
	     !diagram.isNull();
	     diagram = diagram.nextSiblingElement(QStringLiteral("diagram"))) {
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
		if (state.has_work_branch) {
			// 被退回的圖(work 分支圖框已戳「退回修改」):回到繪製者
			// 手上編輯,顯示「編輯中」而非「已入庫未送審」。
			if (state.doc_status.contains(QStringLiteral("退回修改"))
			    || state.doc_status.contains(QLatin1String("Rejected")))
				return tr("編輯中");
			return tr("已入庫未送審");
		}
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
			{state.revision,
			 QFileInfo(path).fileName(),
			 status,
			 state.drawn_by,
			 state.checked_by,
			 state.approved_by});
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
	m_checkout_button->setText(locked_by_me ? tr("出庫編輯")
						: tr("出庫開啟"));
	m_checkin_button->setEnabled(locked_by_me && !in_review);
	m_cancel_button->setEnabled(locked_by_me);
	// 檢視發行版(唯讀):清單內的圖檔都在 main 上,隨時可看發行版
	m_view_released_button->setEnabled(has_selection);

	// 入庫送審(一步):出庫編輯中(我鎖定)、尚無 PR
	m_submit_direct_button->setEnabled(locked_by_me && !in_review);
	// 完成送審:已入庫(work 分支存在)、未鎖定、尚無 PR
	m_submit_button->setEnabled(has_selection && state.has_work_branch
				    && state.lock_owner.isEmpty() && !in_review);
	// 審核檢視:有 PR 即可(唯讀)。「確認」「核准」兩組各一顆,狀態同步。
	m_review_button->setEnabled(in_review);
	m_review_button2->setEnabled(in_review);
	// 開發期暫放寬「非製圖者本人」限制,方便單人自測整套流程;
	// 正式版應恢復 (state.pr_author != m_username) 條件。
	m_approve_button->setEnabled(in_review && m_is_confirmer);
	const bool can_reject = in_review && (m_is_confirmer || m_is_releaser);
	m_reject_button->setEnabled(can_reject);
	m_reject_button2->setEnabled(can_reject);
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
	// 整組「管理員維護」只給核准者;內部按鈕再依選取狀態 enable
	if (m_admin_box) m_admin_box->setVisible(m_is_releaser);
	m_del_folder_button->setEnabled(folder_selected);
	m_del_drawing_button->setEnabled(idle);
}

void PdmDialog::addNewDrawing()
{
	if (busyGuard()) return;
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

	showBusy(true, true, 5);
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
			PdmVersion::nextMinor(QString()), true,
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
	if (busyGuard()) return;
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

	showBusy(true, true, 2);
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
					PdmVersion::nextMinor(fs.revision), true,
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

bool PdmDialog::promptCheckinCommit(const QString &abs_path,
				    const QString &title,
				    const QString &summary_label,
				    QString *commit_message,
				    QString *human_summary)
{
	// 讀已存檔的工作區 .qet,收集各頁修訂欄的修訂項(Phase B:全收,
	// 由使用者勾選;Phase C 會改以「上次發行日期」過濾)。
	QList<PdmRevision::Entry> entries;
	QString work_version;
	QString release_base_date;
	{
		QFile file(abs_path);
		QDomDocument doc;
		if (file.open(QIODevice::ReadOnly) && doc.setContent(&file)) {
			// 只列「自上次發行後」的增修項(首次發行前發行史為空=全收)
			release_base_date = PdmReleaseHistory::lastReleaseDate(doc);
			const QDate since = QDate::fromString(
				release_base_date, QStringLiteral("yyyy-MM-dd"));
			entries = PdmRevision::collectChanges(doc, since);
			work_version = PdmVersion::workVersion(doc);
		}
		file.close();
	}

	QDialog dialog(this);
	dialog.setWindowTitle(title);
	auto *layout = new QVBoxLayout(&dialog);

	auto *summary_lbl = new QLabel(summary_label, &dialog);
	layout->addWidget(summary_lbl);
	auto *summary_edit = new QLineEdit(&dialog);
	layout->addWidget(summary_edit);

	QListWidget *list = nullptr;
	if (entries.isEmpty()) {
		auto *hint = new QLabel(
			tr("(本檔各頁修訂欄沒有可帶入的修訂項;"
			   "如需記錄,請先在圖框「本頁修訂」填寫。)"), &dialog);
		hint->setWordWrap(true);
		layout->addWidget(hint);
	} else {
		layout->addWidget(new QLabel(
			tr("勾選要納入本次入庫記錄的修訂項:"), &dialog));
		list = new QListWidget(&dialog);
		for (const PdmRevision::Entry &e : entries) {
			auto *it = new QListWidgetItem(
				QStringLiteral("P%1  %2  %3")
					.arg(e.folio).arg(e.date, e.desc), list);
			it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
			it->setCheckState(Qt::Checked);
		}
		layout->addWidget(list);
	}

	layout->addWidget(new QLabel(tr("出入庫意見(選填,不進發行記錄):"),
				     &dialog));
	auto *comment_edit = new QPlainTextEdit(&dialog);
	comment_edit->setMaximumHeight(80);
	layout->addWidget(comment_edit);

	auto *buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

	if (dialog.exec() != QDialog::Accepted) return false;

	const QString summary = summary_edit->text().trimmed();
	if (summary.isEmpty()) {
		QMessageBox::warning(this, title, tr("變更摘要不可空白。"));
		return false;
	}

	PdmRevision::CommitMeta meta;
	meta.work_version = work_version;
	meta.release_base_date = release_base_date;
	meta.comment = comment_edit->toPlainText().trimmed();
	if (list) {
		for (int i = 0; i < list->count(); ++i) {
			if (list->item(i)->checkState() == Qt::Checked)
				meta.changes.append(entries.at(i));
		}
	}

	if (human_summary) *human_summary = summary;
	if (commit_message)
		*commit_message = PdmRevision::encodeCommit(summary, meta);
	return true;
}

bool PdmDialog::promptReleaseSelection(
	const QList<PdmRevision::Entry> &entries,
	QList<PdmRevision::Entry> *chosen, QString *message)
{
	QDialog dialog(this);
	dialog.setWindowTitle(tr("核准發行"));
	auto *layout = new QVBoxLayout(&dialog);

	QListWidget *list = nullptr;
	if (entries.isEmpty()) {
		auto *hint = new QLabel(
			tr("(自上次發行後,各頁修訂欄沒有新的增修項;"
			   "首頁發行記錄本次仍會新增一列,但異動摘要為空。)"),
			&dialog);
		hint->setWordWrap(true);
		layout->addWidget(hint);
	} else {
		layout->addWidget(new QLabel(
			tr("勾選要寫入首頁發行記錄的增修項:"), &dialog));
		list = new QListWidget(&dialog);
		for (const PdmRevision::Entry &e : entries) {
			auto *it = new QListWidgetItem(
				QStringLiteral("P%1  %2  %3")
					.arg(e.folio).arg(e.date, e.desc), list);
			it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
			it->setCheckState(Qt::Checked);
		}
		layout->addWidget(list);
	}

	layout->addWidget(new QLabel(tr("簽核訊息(必填,會寫入 commit):"),
				     &dialog));
	auto *msg_edit = new QPlainTextEdit(&dialog);
	msg_edit->setMaximumHeight(80);
	layout->addWidget(msg_edit);

	auto *buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

	if (dialog.exec() != QDialog::Accepted) return false;
	const QString msg = msg_edit->toPlainText().trimmed();
	if (msg.isEmpty()) {
		QMessageBox::warning(this, tr("核准發行"), tr("簽核訊息不可空白。"));
		return false;
	}

	chosen->clear();
	if (list) {
		for (int i = 0; i < list->count(); ++i) {
			if (list->item(i)->checkState() == Qt::Checked)
				chosen->append(entries.at(i));
		}
	}
	*message = msg;
	return true;
}

void PdmDialog::checkIn()
{
	if (busyGuard()) return;
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

	// 入庫前先請編輯器把該檔存檔(同步),git 與修訂項收集才抓得到最新
	// 編輯內容;存好後才彈變更說明對話框(需讀存檔後的修訂欄)。
	const QString abs_path = worktree + '/' + rel_path;
	emit requestSaveFile(abs_path);

	QString message;
	if (!promptCheckinCommit(abs_path, tr("入庫"),
				 tr("變更摘要(必填):"), &message, nullptr))
		return;

	showBusy(true, true, 3);
	// 版本已在出庫時寫入圖框、入庫直接沿用,不再進版。存檔後直接提交。
	m_git->enqueue({"add", "--", rel_path}, worktree, {});
	m_git->enqueue({"commit", "-m", message}, worktree,
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
							if (!r.ok && !lfsUnlockBenign(r.output)) {
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
	if (busyGuard()) return;
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->data(0, Qt::UserRole).toString();
	const QString worktree = worktreeDir(sanitizedStem(rel_path));

	const auto answer = QMessageBox::question(this, tr("取消出庫"),
		tr("將捨棄「%1」出庫後的所有未入庫修改並解除鎖定,確定?")
			.arg(rel_path));
	if (answer != QMessageBox::Yes) return;

	showBusy(true, true, 3);
	if (QDir(worktree).exists()) {
		m_git->enqueue({"checkout", "--", "."}, worktree, {});
	}
	m_git->enqueue({"lfs", "unlock", rel_path}, vaultDir(),
		[this, rel_path, worktree](const PdmGitWorker::Result &result) {
			if (!result.ok && !lfsUnlockBenign(result.output))
				fail(tr("解除鎖定失敗"), result.output);
			setFileWritable(worktree + '/' + rel_path, false);
			// 取消出庫後同樣關掉編輯器裡的該檔(已還原成庫內版本)
			emit requestCloseFile(worktree + '/' + rel_path);
			refresh();
		});
}

void PdmDialog::submitForReview()
{
	if (busyGuard()) return;
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

	showBusy(true, true, 3);

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

void PdmDialog::checkInAndSubmit()
{
	if (busyGuard()) return;
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->data(0, Qt::UserRole).toString();
	const QString stem = sanitizedStem(rel_path);
	const QString branch = workBranchOf(rel_path);
	const QString worktree = worktreeDir(stem);
	const QString vault = vaultDir();
	const QString abs_path = worktree + '/' + rel_path;

	if (!QDir(worktree).exists()) {
		QMessageBox::warning(this, tr("入庫送審"),
			tr("找不到本機工作區,無法入庫送審。"));
		return;
	}

	// 先存檔,再彈變更說明(需讀存檔後的修訂欄)。commit 用結構化訊息,
	// PR body 用純人類摘要(審核者看得清爽,不夾 pdm-meta 區塊)。
	emit requestSaveFile(abs_path);

	QString commit_msg, msg;
	if (!promptCheckinCommit(abs_path, tr("入庫送審"),
				 tr("送審說明(必填,審核者會看到):"),
				 &commit_msg, &msg))
		return;

	// 把「審核中」戳進圖框 → 單一 commit(內容+狀態一起)→ push → 解鎖 → 開 PR。
	if (!stampDocFields(abs_path, QString::fromUtf8(DOC_STATUS_REVIEWING),
			    QString(), false)) {   // 保留出庫時的版本
		fail(tr("寫入圖框欄位失敗"), abs_path);
		return;
	}

	showBusy(true, true, 4);
	auto open_pr = [this, rel_path, stem, branch, worktree, msg]() {
		m_service->createPullRequest(currentRepoFullName(),
			branch, DEFAULT_BRANCH, tr("%1 送審").arg(stem), msg,
			[this, rel_path, worktree](const PdmService::Reply &reply) {
				showBusy(false);
				if (!reply.ok) {
					fail(tr("送審失敗"), reply.error);
					return;
				}
				const int pr = reply.json.object()
					.value(QStringLiteral("number")).toInt();
				setFileWritable(worktree + '/' + rel_path, false);
				emit requestCloseFile(worktree + '/' + rel_path);
				m_status_label->setText(tr("「%1」已入庫並送審(#%2)")
					.arg(rel_path).arg(pr));
				refresh();
			});
	};
	m_git->enqueue({"add", "--", rel_path}, worktree, {});
	m_git->enqueue({"commit", "-m", commit_msg}, worktree,
		[this, branch, worktree, vault, rel_path, open_pr]
		(const PdmGitWorker::Result &commit_r) {
			const bool nothing = !commit_r.ok
				&& commit_r.output.contains(QStringLiteral("nothing"));
			if (!commit_r.ok && !nothing) {
				fail(tr("入庫送審 commit 失敗"), commit_r.output);
				return;
			}
			m_git->enqueue({"push", "-u", "origin", branch}, worktree,
				[this, vault, rel_path, open_pr]
				(const PdmGitWorker::Result &push_r) {
					if (!push_r.ok) {
						fail(tr("推送失敗,已保留出庫狀態,"
							"請檢查網路後重試"),
						     push_r.output);
						return;
					}
					m_git->enqueue({"lfs", "unlock", rel_path},
						vault,
						[open_pr](const PdmGitWorker::Result &r) {
							Q_UNUSED(r);   // 解鎖失敗不擋送審
							open_pr();
						});
				});
		});
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

	showBusy(true, true, 2);
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
	if (busyGuard()) return;
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->data(0, Qt::UserRole).toString();
	const QString vault = vaultDir();
	// main 上最後發行版,以 detached worktree 唯讀開啟(含完整倉庫上下文,
	// 開檔乾淨無整合提示;與唯讀瀏覽、發行版檢視一致)。不用 git show 到
	// 孤立暫存檔——那缺元件庫/圖框範本,且暫存檔設唯讀後再開會「無法建立
	// 暫存檔」。
	const QString view_dir = PdmSettings::workRoot() + '/' + currentRepoFullName()
		+ QStringLiteral("/latest-view");
	showBusy(true, true, 3);
	m_git->enqueue({"fetch", "origin", "--prune"}, vault, {});
	if (QDir(view_dir).exists())
		m_git->enqueue({"worktree", "remove", "--force", view_dir}, vault, {});
	m_git->enqueue({"worktree", "add", "--detach", view_dir,
		QStringLiteral("origin/") + QLatin1String(DEFAULT_BRANCH)}, vault,
		[this, rel_path, view_dir](const PdmGitWorker::Result &r) {
			if (!r.ok) { fail(tr("讀取發行版失敗"), r.output); return; }
			setFileWritable(view_dir + '/' + rel_path, false);
			emit requestOpenFile(view_dir + '/' + rel_path);
			m_status_label->setText(
				tr("已開啟「%1」發行版(唯讀)").arg(rel_path));
		});
}

void PdmDialog::confirmDone()
{
	if (busyGuard()) return;
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
		tr("確認完畢 by %1 / 意見：%2").arg(m_username, note.trimmed()),
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
	if (busyGuard()) return;
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
		tr("退回 by %1 / 意見：%2").arg(m_username, trimmed),
		[this, rel_path, pr_index, trimmed]() {
			m_service->submitReview(currentRepoFullName(), pr_index,
				QStringLiteral("REQUEST_CHANGES"), trimmed,
				[this, rel_path, pr_index](const PdmService::Reply &reply) {
					// 開發期單人測試:Gitea 擋「審自己的 PR」(422)。
					// 退回意見與「退回修改」commit 已推上 work 分支
					// (真正有意義的部分),422 視為已退回不再報錯。
					if (!reply.ok && reply.http_status != 422) {
						showBusy(false);
						fail(tr("退回失敗"), reply.error);
						return;
					}
					// 退回=離開審核:關閉 PR,讓圖回到繪製者「編輯中」,
					// 繪製者才能再出庫修改(否則 pr 還在會一直卡審核中)。
					m_service->closePullRequest(currentRepoFullName(),
						pr_index,
						[this, rel_path](const PdmService::Reply &close_r) {
							showBusy(false);
							if (!close_r.ok) {
								fail(tr("退回後關閉審核失敗"),
								     close_r.error);
								return;
							}
							m_status_label->setText(
								tr("「%1」已退回").arg(rel_path));
							refresh();
						});
				});
		});
}

void PdmDialog::approveAndRelease()
{
	if (busyGuard()) return;
	const QTreeWidgetItem *item = selectedFileItem();
	if (!item) return;
	const QString rel_path = item->data(0, Qt::UserRole).toString();
	const FileState state = m_files.value(rel_path);
	if (state.pr_index <= 0) return;
	const int pr_index = state.pr_index;
	const QString stem = sanitizedStem(rel_path);
	const QString vault = vaultDir();

	const QString branch = workBranchOf(rel_path);
	const QString release_version = PdmVersion::nextMajor(state.revision);

	// 讀 work 分支最終送審內容,算「自上次發行後的增修項」供放行者勾選。
	m_git->enqueue({QStringLiteral("show"),
		QStringLiteral("origin/%1:%2").arg(branch, rel_path)}, vault,
		[this, rel_path, pr_index, stem, vault, branch, release_version]
		(const PdmGitWorker::Result &show_r) {
		QList<PdmRevision::Entry> entries;
		if (show_r.ok) {
			QDomDocument doc;
			if (doc.setContent(show_r.output)) {
				const QString last = PdmReleaseHistory::lastReleaseDate(doc);
				const QDate since = QDate::fromString(
					last, QStringLiteral("yyyy-MM-dd"));
				entries = PdmRevision::collectChanges(doc, since);
			}
		}

		QList<PdmRevision::Entry> chosen;
		QString msg;
		if (!promptReleaseSelection(entries, &chosen, &msg)) return;

		const QString changes_str = PdmRevision::formatChanges(chosen);
		const QString release_date =
			QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));

		// 戳記後、commit 前:append 首頁發行史列 + 寫 pdm_release_version。
		// 在 work 分支寫入,隨合併帶進受保護的 main。
		auto post_stamp = [this, release_version, release_date, changes_str]
			(const QString &abs_path) -> bool {
			QFile f(abs_path);
			QDomDocument doc;
			const bool loaded = f.open(QIODevice::ReadOnly)
				&& doc.setContent(&f);
			f.close();
			if (!loaded) return false;
			PdmReleaseHistory::appendRelease(doc,
				{release_version, release_date, m_username, changes_str});
			PdmVersion::setProjectProperty(doc,
				QLatin1String(PdmVersion::RELEASE_VERSION),
				release_version);
			// 首頁自動渲染:把整份發行史(含本次)寫成文件管制頁上的
			// 多行等寬文字圖元(見設計 §5.2)。等寬字型讓欄位對齊。
			QFont mono(QStringLiteral("Menlo"));
			mono.setStyleHint(QFont::Monospace);
			mono.setPointSize(6);
			PdmReleaseHistory::upsertHistoryTextItem(doc,
				PdmReleaseHistory::formatHistoryText(
					PdmReleaseHistory::readReleases(doc)),
				mono.toString(), 20.0, 20.0);
			QFile out(abs_path);
			if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
				return false;
			QTextStream ts(&out);
			ts << doc.toString(2);
			out.close();
			return true;
		};

	// 先在 work 分支寫核准者 + 已發行狀態 + 發行史 + commit,再核准最新 head
	// 並合併發行。順序:先 commit(改 head)、後核准 → 不觸發「廢止過時核准」。
	signoffOnWorkBranch(rel_path, QString::fromUtf8(DOC_STATUS_RELEASED),
		{{QStringLiteral("approved-by"), m_username}},
		tr("核准發行 by %1 / 意見：%2").arg(m_username, msg),
		[this, rel_path, pr_index, stem, vault, msg]() {
		// 推下一發行版次 → 合併 → 發行(核准成功或自我核准被拒都走這)
		auto merge_and_release = [this, rel_path, pr_index, stem, vault]() {
			// 先取剛推上去的「核准發行」commit SHA(work 分支工作區的 HEAD),
			// 合併時指定它為 head_commit_id,保證併進 main 的是這顆正式發行
			// commit,不會誤併到舊 head(消除 push 與 Gitea 索引的競態)。
			const QString worktree = worktreeDir(stem);
			m_git->enqueue({"rev-parse", "HEAD"}, worktree,
				[this, rel_path, pr_index, stem, vault]
				(const PdmGitWorker::Result &sha_r) {
				const QString head_sha = sha_r.output.trimmed();
				m_git->enqueue({"ls-remote", "--tags", "origin",
					QStringLiteral("refs/tags/release/%1-v*").arg(stem)},
					vault,
					[this, rel_path, pr_index, stem, vault, head_sha]
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
				}, 3, head_sha);
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
	// 發行進版:把版本推到下一個主版(0.x→1.0、1.x→2.0),再真正發行。
	// 發行步驟多(簽核+合併+渲染 PDF+建 Release+清理),預期步數給大一點。
	}, true, release_version, 15, post_stamp);
	});   // 關閉 git show 回呼
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

	showBusy(true, true, 2);
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
	if (busyGuard()) return;
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

	showBusy(true, true, 4);
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
	if (busyGuard()) return;
	const QString vault = vaultDir();
	showBusy(true, true, 5);
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
					// 新 commit 版本在專案級,舊 commit 退回首頁 indexrev
					(*recs)[i].version = PdmVersion::workVersion(doc);
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
					// 核准發行的版本:有發行 tag,或 commit 訊息是
					// 「核准發行…」(tag 落在合併 commit,發行 commit
					// 本身不帶 tag,故也用訊息判斷),都要底色 highlight。
					const bool is_release = !rec.rel_tag.isEmpty()
						|| rec.subject.startsWith(
							QStringLiteral("核准發行"));
					if (is_release) {
						// 有 tag 才可雙擊唯讀開啟該發行版
						const bool has_tag = !rec.rel_tag.isEmpty();
						auto *item = new QTreeWidgetItem(
							m_history_tree,
							{(has_tag ? QStringLiteral("★ ")
								  : QString())
							 + rec.version,
							 rec.datetime, rec.committer,
							 has_tag ? tr("正式發行")
								 : rec.subject});
						for (int c = 0; c < 4; ++c) {
							item->setBackground(c, highlight);
							QFont fnt = item->font(c);
							fnt.setBold(true);
							item->setFont(c, fnt);
						}
						if (has_tag)
							item->setData(0, Qt::UserRole,
								      rec.rel_tag);
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
	showBusy(true, true, 2);
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

	// 漸進式進度條:每個 git 步驟往前逼近(見 stepStarted),完成時到 100%。
	// 不用忙碌動畫(range 0,0)——它在此樣式下常渲染成滿格,看不出在跑。
	m_busy_bar = new QProgressBar(m_busy_dialog);
	m_busy_bar->setRange(0, 100);
	m_busy_bar->setValue(0);
	m_busy_bar->setTextVisible(false);
	m_busy_bar->setFixedHeight(6);
	lay->addWidget(m_busy_bar);
}

bool PdmDialog::busyGuard()
{
	// 只擋「使用者操作進行中」時的新操作(背景重整不算),避免兩個操作的
	// git(尤其 reset --hard)並行互相破壞 staging。
	if (m_op_active) {
		QMessageBox::information(this, tr("圖檔管理"),
			tr("目前有作業進行中,請等它完成後再操作。"));
		return true;
	}
	return false;
}

void PdmDialog::showBusy(bool busy, bool with_dialog, int op_steps)
{
	// 進度對話框/收框由 git 活動驅動(stepStarted 前進、allFinished 防抖收),
	// 這裡只負責:(1) 標記使用者操作進行中(供 busyGuard),(2) 使用者操作
	// 一按下就立刻顯示框並設定預期步數(進度條以 已完成/預期 前進)。
	if (busy) {
		if (with_dialog) {
			// 使用者操作開始前,清掉還排隊中的背景讀取(重整/歷史)git,
			// 讓本操作的步驟獨佔佇列——否則背景步驟會被算進本操作的進度,
			// 進度條就會「一出現就跳到很高、再亂跳」。
			m_git->cancelPending();
			m_op_active = true;
			m_op_total = op_steps;
			m_op_done = 0;
			ensureBusyDialog();
			m_busy_label->setText(tr("處理中…"));
			m_busy_bar->setValue(0);
			m_hide_timer->stop();
			m_busy_dialog->show();
			m_busy_dialog->raise();
			m_refresh_button->setEnabled(false);
			m_repo_combo->setEnabled(false);
		}
	} else {
		m_op_active = false;   // 收框交給防抖計時器,這裡不強制隱藏
	}
}

void PdmDialog::fail(const QString &title, const QString &log)
{
	// 失敗:立刻收框(不等防抖)並恢復按鈕
	if (m_hide_timer) m_hide_timer->stop();
	if (m_busy_dialog) m_busy_dialog->hide();
	m_op_active = false;
	m_refresh_button->setEnabled(true);
	m_repo_combo->setEnabled(true);
	m_status_label->setText(title);
	// 認得的 git / Gitea 錯誤轉成中文友善說明並隱藏原文;認不得的才附原文供通報。
	const QString friendly = translateError(log);
	if (friendly.isEmpty())
		QMessageBox::warning(this, title,
			tr("發生未預期的錯誤,請將下列訊息回報管理員:\n\n%1")
			.arg(log.right(1500)));
	else
		QMessageBox::warning(this, title, friendly);
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

void PdmDialog::submitDirectByPath(const QString &abs_path)
{
	if (selectFileInUi(drawingRelPath(abs_path))) checkInAndSubmit();
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
	if (busyGuard()) return;
	// 一律抓伺服器最新版(origin/main),不打開本機舊快取。用 detached worktree
	// 開啟(而非 git show 到 /tmp 的孤立單檔)——後者缺少圖庫上下文(元件庫、
	// 圖框範本),QET 開檔時會跳整合/找不到資源的對話框。worktree 與審核/發行版
	// 檢視一致,含完整倉庫內容,開檔乾淨無提示。
	const QString rel = drawingRelPath(abs_path);
	if (rel.isEmpty()) {
		show(); raise(); activateWindow(); refresh();
		return;
	}
	const QString vault = vaultDir();
	const QString view_dir = PdmSettings::workRoot() + '/' + currentRepoFullName()
		+ QStringLiteral("/latest-view");
	showBusy(true, true, 3);
	m_git->enqueue({"fetch", "origin", "--prune"}, vault, {});
	// 每次重建 detached worktree 到最新 origin/main(移除舊的以免唯讀檔擋 checkout)
	if (QDir(view_dir).exists())
		m_git->enqueue({"worktree", "remove", "--force", view_dir}, vault, {});
	m_git->enqueue({"worktree", "add", "--detach", view_dir,
		QStringLiteral("origin/") + QLatin1String(DEFAULT_BRANCH)}, vault,
		[this, rel, view_dir](const PdmGitWorker::Result &r) {
		if (!r.ok) {
			fail(tr("無法瀏覽最新版(此圖可能尚未發行到 main)"), r.output);
			return;
		}
		setFileWritable(view_dir + '/' + rel, false);
		emit requestOpenFile(view_dir + '/' + rel);
		showBusy(false);
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
