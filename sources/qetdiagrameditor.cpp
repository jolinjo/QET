/*
	Copyright 2006-2026 The QElectroTech Team
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
#include "qetdiagrameditor.h"

#include <QListWidget>
#include <QStyledItemDelegate>
#include <QSettings>
#include <QToolBar>
#include <QToolButton>
#include <QPainter>
#include <QPixmap>
#include <QIcon>
#include <QProxyStyle>
#include <QStyleOptionToolButton>

namespace {
// 讓「圖示在上、文字在下」的工具列按鈕，文字固定貼在按鈕底部、圖示置中於上方，
// 使所有標籤落在同一底線——不受各圖示原生高度不一影響。
class ToolBarBottomTextStyle : public QProxyStyle
{
public:
	explicit ToolBarBottomTextStyle(QObject *parent = nullptr) : QProxyStyle()
	{ setParent(parent); }

	void drawControl(ControlElement ce, const QStyleOption *opt,
					 QPainter *p, const QWidget *w) const override
	{
		const auto *tb = qstyleoption_cast<const QStyleOptionToolButton *>(opt);
		if (ce != CE_ToolButtonLabel || !tb
			|| tb->toolButtonStyle != Qt::ToolButtonTextUnderIcon
			|| tb->icon.isNull() || tb->text.isEmpty()) {
			QProxyStyle::drawControl(ce, opt, p, w);
			return;
		}

		QRect rect = tb->rect;
		if (tb->state & (State_Sunken | State_On)) {
			rect.translate(proxy()->pixelMetric(PM_ButtonShiftHorizontal, tb, w),
						   proxy()->pixelMetric(PM_ButtonShiftVertical, tb, w));
		}

		const int text_h = tb->fontMetrics.height();
		const QRect text_rect(rect.left(), rect.bottom() - text_h + 1,
							  rect.width(), text_h);

		QIcon::Mode mode = (tb->state & State_Enabled) ? QIcon::Normal
													   : QIcon::Disabled;
		if (mode == QIcon::Normal && (tb->state & State_MouseOver)
			&& (tb->activeSubControls & SC_ToolButton))
			mode = QIcon::Active;
		const QIcon::State st = (tb->state & State_On) ? QIcon::On : QIcon::Off;
		const QPixmap pm = tb->icon.pixmap(tb->iconSize, mode, st);

		// 圖示置中於「文字上方」的區域
		const QRect icon_area(rect.left(), rect.top(),
							  rect.width(), rect.height() - text_h);
		const qreal dpr = pm.devicePixelRatio();
		QRect ir(0, 0, qRound(pm.width() / dpr), qRound(pm.height() / dpr));
		ir.moveCenter(icon_area.center());
		p->drawPixmap(ir, pm);

		proxy()->drawItemText(p, text_rect, Qt::AlignHCenter | Qt::AlignBottom,
							  tb->palette, tb->state & State_Enabled, tb->text,
							  QPalette::ButtonText);
	}
};
}
#include <QMenuBar>
#include <QMenu>
#include "qetversion.h"
#include <QCoreApplication>
#include "ElementsCollection/elementscollectionwidget.h"
#include "QWidgetAnimation/qwidgetanimation.h"
#include "autoNum/ui/autonumberingdockwidget.h"
#include "Pdm/pdmdialog.h"
#include "conductornumexport.h"
#include "diagramcommands.h"
#include "diagramevent/diagrameventaddimage.h"
#include "diagramevent/diagrameventaddshape.h"
#include "diagramevent/diagrameventaddtext.h"
#include "diagramview.h"
#include "elementspanelwidget.h"
#include "factory/qetgraphicstablefactory.h"
#include "print/projectprintwindow.h"
#include "qetgraphicsitem/ViewItem/qetgraphicstableitem.h"
#include "qetgraphicsitem/conductortextitem.h"
#include "qetgraphicsitem/dynamicelementtextitem.h"
#include "qeticons.h"
#include "qetmessagebox.h"
#include "recentfiles.h"
#include "ui/bomexportdialog.h"
#include "ui/diagrampropertieseditordockwidget.h"
#include "ui/dialogwaiting.h"
#include "ui/foliorevisionsdialog.h"
#include "undocommand/addelementtextcommand.h"
#include "undocommand/rotateselectioncommand.h"
#include "undocommand/rotatetextscommand.h"
#include "diagram.h"
#include "TerminalStrip/ui/terminalstripeditorwindow.h"
#include "ui/diagrameditorhandlersizewidget.h"
#include "TerminalStrip/ui/addterminalstripitemdialog.h"
#include "wiringlistexport.h"
#include "ui/terminalnumberingdialog.h"
#include <QDebug>
#ifdef BUILD_WITHOUT_KF5
#else
#	include <KAutoSaveFile>
#endif

/**
	@brief QETDiagramEditor::QETDiagramEditor
	Constructor
	@param files : list of files to open
	@param parent : parent widget
*/
QETDiagramEditor::QETDiagramEditor(const QStringList &files, QWidget *parent) :
	QETMainWindow(parent),
	m_row_column_actions_group (this),
	m_selection_actions_group  (this),
	m_add_item_actions_group   (this),
	m_zoom_actions_group       (this),
	m_select_actions_group     (this),
	m_file_actions_group       (this),
	open_dialog_dir            (QETApp::documentDir())
{
		//Trivial property use to set the graphics handler size
	setProperty("graphics_handler_size", 10);

	activeSubWindowIndex = 0;

	QSplitter *splitter_ = new QSplitter(this);
	splitter_->setChildrenCollapsible(false);
	splitter_->setOrientation(Qt::Vertical);
	splitter_->addWidget(&m_workspace);
	splitter_->addWidget(&m_search_and_replace_widget);
	setCentralWidget(splitter_);
	m_search_and_replace_widget.setEditor(this);

	QList<int> s;
	s << m_workspace.maximumHeight() << m_search_and_replace_widget.minimumSizeHint().height();
	splitter_->setSizes(s); //Force the size of the search and replace widget, force have a good animation the first time he is showed

	auto anim = new QWidgetAnimation(&m_search_and_replace_widget, Qt::Vertical, QWidgetAnimation::lastSize, 250);
	anim->setObjectName("search and replace animator");
	m_search_and_replace_widget.setHidden(true);
	anim->setLastShowSize(m_search_and_replace_widget.minimumSizeHint().height());

		//Set object name to be retrieved by the stylesheets
	m_workspace.setBackground(QBrush(Qt::NoBrush));
	m_workspace.setObjectName("mdiarea");
	m_workspace.setTabsClosable(true);

		//Set the signal mapper
	connect(&windowMapper, &QSignalMapper::mappedObject, this,
		[this](QObject *o) { activateWidget(qobject_cast<QWidget *>(o)); });

	setWindowTitle(tr("QElectroTech", "window title") + " " + QetVersion::displayedVersion());
	setWindowIcon(QET::Icons::QETLogo);
	statusBar() -> showMessage(tr("QElectroTech", "status bar message"));

	setUpElementsPanel();
	setUpElementsCollectionWidget();
	setUpUndoStack();
	setUpSelectionPropertiesEditor();
	setUpAutonumberingWidget();

	setUpActions();
	setUpToolBar();
	setUpMenu();

	applyInterfaceFonts();

	// 啟動後背景預連圖檔管理伺服器,依結果啟用/禁用工具列按鈕
	setUpPdmBackgroundConnect();
	updatePdmToolbar();   // 初始:無受管檔開啟→隱藏入庫/確認等動作
	applyReadOnlyView(false);   // 初始無開圖→隱藏編輯類工具鈕(has_project=false)

	tabifyDockWidget(qdw_undo, qdw_pa);

		//By default the windows is maximised
	setMinimumSize(QSize(500, 350));
	setWindowState(Qt::WindowMaximized);

	connect (&m_workspace,
		 SIGNAL(subWindowActivated(QMdiSubWindow *)),
		 this,
		 SLOT(subWindowActivated(QMdiSubWindow*)));
	connect (QApplication::clipboard(),
		 SIGNAL(dataChanged()),
		 this,
		 SLOT(slot_updatePasteAction()));

	setUpWelcomeWidget();

	readSettings();
	show();

		//If valid file path is given as arguments
	uint opened_projects = 0;
	if (files.count())
	{
			//So we open this files
		foreach(QString file, files)
			if (openAndAddProject(file))
				++ opened_projects;
	}

	slot_updateActions();
	updateWelcomeWidget();
}

/**
	Destructeur
*/
QETDiagramEditor::~QETDiagramEditor()
{
}

namespace
{
	/* rendu « carte » des fichiers recents de la vue d'accueil :
	 * nom du fichier en gras, chemin en petit et gris dessous */
	class RecentFileDelegate : public QStyledItemDelegate
	{
		public:
		using QStyledItemDelegate::QStyledItemDelegate;
		static const int ROW_HEIGHT = 58;
		static const int HEADER_HEIGHT = 40;
		enum { PathRole = Qt::UserRole,
		       HeaderRole = Qt::UserRole + 1,
		       DateRole = Qt::UserRole + 2 };

		void paint(QPainter *painter,
			   const QStyleOptionViewItem &option,
			   const QModelIndex &index) const override
		{
			painter->save();
			painter->setRenderHint(QPainter::Antialiasing);
			const QRect cell = option.rect.adjusted(2, 2, -2, -2);

			//entete de groupe (aujourd'hui / hier / ...)
			if (index.data(HeaderRole).toBool()) {
				QFont header_font = option.font;
				header_font.setBold(true);
				header_font.setPointSizeF(
					header_font.pointSizeF() * 0.9);
				painter->setFont(header_font);
				QColor grey = option.palette.text().color();
				grey.setAlpha(150);
				painter->setPen(grey);
				painter->drawText(
					cell.adjusted(6, 0, -6, -4),
					Qt::AlignLeft | Qt::AlignBottom,
					index.data().toString());
				painter->restore();
				return;
			}

			if (option.state
			    & (QStyle::State_Selected | QStyle::State_MouseOver)) {
				QColor hover = option.palette.highlight().color();
				hover.setAlpha(option.state & QStyle::State_Selected
					       ? 60 : 30);
				painter->setPen(Qt::NoPen);
				painter->setBrush(hover);
				painter->drawRoundedRect(cell, 6, 6);
			}

			const QRect icon_rect(cell.left() + 10,
					      cell.top() + (cell.height() - 32) / 2,
					      32, 32);
			QET::Icons::ProjectFile.paint(painter, icon_rect);

			const int text_x = icon_rect.right() + 12;
			const QRect name_rect(text_x, cell.top() + 6,
					      cell.right() - text_x - 8,
					      cell.height() / 2 - 6);
			const QRect path_rect(text_x, name_rect.bottom(),
					      cell.right() - text_x - 8,
					      cell.bottom() - name_rect.bottom() - 4);

			//date relative a droite de la ligne du nom
			QFont date_font = option.font;
			date_font.setPointSizeF(date_font.pointSizeF() * 0.85);
			const QString date_text = index.data(DateRole).toString();
			const int date_width =
				QFontMetrics(date_font).horizontalAdvance(date_text)
				+ 8;
			painter->setFont(date_font);
			QColor date_grey = option.palette.text().color();
			date_grey.setAlpha(140);
			painter->setPen(date_grey);
			painter->drawText(name_rect,
					  Qt::AlignRight | Qt::AlignVCenter,
					  date_text);

			QFont name_font = option.font;
			name_font.setBold(true);
			name_font.setPointSizeF(name_font.pointSizeF() * 1.1);
			painter->setFont(name_font);
			painter->setPen(option.palette.text().color());
			painter->drawText(name_rect,
					  Qt::AlignLeft | Qt::AlignVCenter,
					  QFontMetrics(name_font).elidedText(
						  index.data().toString(),
						  Qt::ElideMiddle,
						  name_rect.width()
							  - date_width));

			QFont path_font = option.font;
			path_font.setPointSizeF(path_font.pointSizeF() * 0.85);
			painter->setFont(path_font);
			QColor grey = option.palette.text().color();
			grey.setAlpha(140);
			painter->setPen(grey);
			painter->drawText(path_rect,
					  Qt::AlignLeft | Qt::AlignVCenter,
					  QFontMetrics(path_font).elidedText(
						  index.data(Qt::UserRole).toString(),
						  Qt::ElideMiddle,
						  path_rect.width()));
			painter->restore();
		}

		QSize sizeHint(const QStyleOptionViewItem &,
			       const QModelIndex &index) const override
		{
			return QSize(0, index.data(HeaderRole).toBool()
					     ? HEADER_HEIGHT : ROW_HEIGHT);
		}
	};
}

/**
	Welcome view shown in the workspace when no project is opened :
	the recently opened files, a single click opens one.
*/
void QETDiagramEditor::setUpWelcomeWidget()
{
	m_welcome_widget = new QWidget(m_workspace.viewport());

	auto *title = new QLabel(
		tr("Fichiers récents", "welcome view"), m_welcome_widget);
	QFont title_font = title->font();
	title_font.setPointSizeF(title_font.pointSizeF() * 1.8);
	title_font.setBold(true);
	title->setFont(title_font);
	title->setAlignment(Qt::AlignHCenter);

	m_welcome_list = new QListWidget(m_welcome_widget);
	m_welcome_list->setFixedWidth(720);
	m_welcome_list->setFrameShape(QFrame::NoFrame);
	m_welcome_list->setStyleSheet(
		QStringLiteral("QListWidget { background: transparent; }"));
	m_welcome_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_welcome_list->setMouseTracking(true);
	m_welcome_list->setItemDelegate(new RecentFileDelegate(m_welcome_list));
	m_welcome_list->setCursor(Qt::PointingHandCursor);
	connect(m_welcome_list, &QListWidget::itemClicked, this,
		[this](QListWidgetItem *item) {
		openRecentFile(item->data(Qt::UserRole).toString());
	});

	auto *layout = new QVBoxLayout(m_welcome_widget);
	layout->addStretch(2);
	layout->addWidget(title, 0, Qt::AlignHCenter);
	layout->addSpacing(14);
	layout->addWidget(m_welcome_list, 0, Qt::AlignHCenter);
	layout->addStretch(3);

	m_workspace.viewport()->installEventFilter(this);
}

/**
	Show the welcome view when the workspace is empty, hide it otherwise.
*/
void QETDiagramEditor::updateWelcomeWidget()
{
	if (!m_welcome_widget) return;

	const bool show_welcome = m_workspace.subWindowList().isEmpty();
	if (show_welcome) {
		m_welcome_list->clear();

		//fichiers existants, tries du plus recemment modifie au plus ancien
		QList<QPair<QDateTime, QString>> entries;
		const QList<QString> files =
			QETApp::projectsRecentFiles()->files();
		for (const QString &file : files) {
			QFileInfo info(file);
			if (!info.exists()) continue;
			//date de derniere ouverture dans QET ; les entrees
			//anterieures a cet horodatage retombent sur la date
			//de modification du fichier
			QDateTime opened =
				QETApp::projectsRecentFiles()->lastOpened(file);
			entries.append({opened.isValid()
						? opened
						: info.lastModified(),
					file});
		}
		std::sort(entries.begin(), entries.end(),
			  [](const QPair<QDateTime, QString> &a,
			     const QPair<QDateTime, QString> &b) {
			return a.first > b.first;
		});

		const QDate today = QDate::currentDate();

		// 依開啟來源分兩區:圖檔管理(PDM)在上、本地檔案在下。
		// 各區內部維持原本的最近開啟排序。
		QList<QPair<QDateTime, QString>> pdm_entries, local_entries;
		for (const auto &entry : entries) {
			if (PdmDialog::isManagedPath(entry.second))
				pdm_entries.append(entry);
			else
				local_entries.append(entry);
		}

		int total_height = 8;
		const auto add_section =
			[&](const QString &label,
			    const QList<QPair<QDateTime, QString>> &list) {
			if (list.isEmpty()) return;
			auto *header = new QListWidgetItem(label, m_welcome_list);
			header->setFlags(Qt::NoItemFlags);
			header->setData(RecentFileDelegate::HeaderRole, true);
			total_height += RecentFileDelegate::HEADER_HEIGHT;
			for (const auto &entry : list) {
				const QDate date = entry.first.date();
				const qint64 days = date.daysTo(today);
				QString date_text;
				if (days <= 0) {
					date_text = tr("Aujourd'hui", "welcome view");
				} else if (days == 1) {
					date_text = tr("Hier", "welcome view");
				} else if (days <= 30) {
					date_text = tr("Il y a %1 jours",
						       "welcome view").arg(days);
				} else {
					date_text = date.toString(
						QStringLiteral("yyyy/M/d"));
				}
				auto *item = new QListWidgetItem(
					QFileInfo(entry.second).completeBaseName(),
					m_welcome_list);
				item->setData(RecentFileDelegate::PathRole,
					      entry.second);
				item->setData(RecentFileDelegate::DateRole, date_text);
				item->setToolTip(entry.second);
				total_height += RecentFileDelegate::ROW_HEIGHT;
			}
		};
		add_section(tr("圖檔管理 PDM", "welcome view"), pdm_entries);
		add_section(tr("本地檔案 Local", "welcome view"), local_entries);

		m_welcome_list->setFixedHeight(qMin(
			total_height,
			qMax(400, m_workspace.viewport()->height() * 3 / 4)));
		m_welcome_widget->setGeometry(m_workspace.viewport()->rect());
		m_welcome_widget->raise();
	}
	m_welcome_widget->setVisible(show_welcome);
}

bool QETDiagramEditor::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == m_workspace.viewport()
	    && event->type() == QEvent::Resize
	    && m_welcome_widget && m_welcome_widget->isVisible()) {
		m_welcome_widget->setGeometry(m_workspace.viewport()->rect());
	}
	return QETMainWindow::eventFilter(watched, event);
}

/**
	@brief QETDiagramEditor::setUpElementsPanel
	Setup the element panel and element panel widget
*/
void QETDiagramEditor::setUpElementsPanel()
{
	//Add the element panel as a QDockWidget
	qdw_pa = new QDockWidget(tr("Projets", "dock title"), this);

	qdw_pa -> setObjectName   ("projects panel");
	qdw_pa -> setAllowedAreas (Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	qdw_pa -> setFeatures     (
				QDockWidget::DockWidgetClosable
				|QDockWidget::DockWidgetMovable
				|QDockWidget::DockWidgetFloatable);
	qdw_pa -> setMinimumWidth (160);
	qdw_pa -> setWidget       (pa = new ElementsPanelWidget(qdw_pa));

	addDockWidget(Qt::LeftDockWidgetArea, qdw_pa);

	connect(pa, SIGNAL(requestForProject                  (QETProject *)), this, SLOT(activateProject(QETProject *)));
	connect(pa, SIGNAL(requestForProjectClosing           (QETProject *)), this, SLOT(closeProject(QETProject *)));
	connect(pa, SIGNAL(requestForProjectPropertiesEdition (QETProject *)), this, SLOT(editProjectProperties(QETProject *)));
	connect(pa, SIGNAL(requestForNewDiagram               (QETProject *)), this, SLOT(addDiagramToProject(QETProject *)));
	connect(pa, SIGNAL(requestForDiagramPropertiesEdition (Diagram *)), this, SLOT(editDiagramProperties(Diagram *)));
	connect(pa, SIGNAL(requestForDiagramsDeletion         (const QList<Diagram *> &)), this, SLOT(removeDiagrams(const QList<Diagram *> &)));
	connect(pa, SIGNAL(requestForDiagramMoveUp			  (const QList<Diagram *> &)), this, SLOT(moveDiagramUp(const QList<Diagram *>&)));
	connect(pa, SIGNAL(requestForDiagramMoveDown		  (const QList<Diagram *> &)), this, SLOT(moveDiagramDown(const QList<Diagram *>&)));
	connect(pa, SIGNAL(requestForDiagramMoveUpTop		  (const QList<Diagram *> &)), this, SLOT(moveDiagramUpTop(const QList<Diagram *>&)));
	connect(pa, SIGNAL(requestForDiagramMoveUpx10		  (const QList<Diagram *> &)), this, SLOT(moveDiagramUpx10(const QList<Diagram *>&)));
	connect(pa, SIGNAL(requestForDiagramMoveDownx10		  (const QList<Diagram *> &)), this, SLOT(moveDiagramDownx10(const QList<Diagram *>&)));
	connect(pa, SIGNAL(requestForDiagramMoveUpx100		  (const QList<Diagram *> &)), this, SLOT(moveDiagramUpx100(const QList<Diagram *>&)));
	connect(pa, SIGNAL(requestForDiagramMoveDownx100	  (const QList<Diagram *> &)), this, SLOT(moveDiagramDownx100(const QList<Diagram *>&)));
}

/**
	@brief QETDiagramEditor::setUpElementsCollectionWidget
	Set up the dock widget of element collection
*/
void QETDiagramEditor::setUpElementsCollectionWidget()
{
	m_qdw_elmt_collection = new QDockWidget(tr("Collections"), this);
	m_qdw_elmt_collection->setObjectName("elements_collection_widget");
	m_qdw_elmt_collection->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	m_qdw_elmt_collection->setFeatures(
				QDockWidget::DockWidgetClosable
				|QDockWidget::DockWidgetMovable
				|QDockWidget::DockWidgetFloatable);

	m_element_collection_widget = new ElementsCollectionWidget(m_qdw_elmt_collection);
	m_qdw_elmt_collection->setWidget(m_element_collection_widget);
	m_element_collection_widget->expandFirstItems();

	addDockWidget(Qt::RightDockWidgetArea, m_qdw_elmt_collection);
}

/**
	@brief QETDiagramEditor::setUpUndoStack
	Setup the undostack and undo stack widget
*/
void QETDiagramEditor::setUpUndoStack()
{

	QUndoView *undo_view = new QUndoView(&undo_group, this);

	undo_view -> setEmptyLabel (tr("Aucune modification"));
	undo_view -> setStatusTip  (tr("Cliquez sur une action pour revenir en arrière dans l'édition de votre schéma", "Status tip"));
	undo_view -> setWhatsThis  (tr("Ce panneau liste les différentes actions effectuées sur le folio courant. Cliquer sur une action permet de revenir à l'état du schéma juste après son application.", "\"What's this\" tip"));

	qdw_undo  = new QDockWidget(tr("Annulations", "dock title"), this);
	qdw_undo -> setObjectName("diagram_undo");

	qdw_undo -> setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	qdw_undo -> setFeatures(
				QDockWidget::DockWidgetClosable
				|QDockWidget::DockWidgetMovable
				|QDockWidget::DockWidgetFloatable);
	qdw_undo -> setMinimumWidth(160);
	qdw_undo -> setWidget(undo_view);

	addDockWidget(Qt::LeftDockWidgetArea, qdw_undo);
}

/**
	@brief QETDiagramEditor::setUpSelectionPropertiesEditor
	Setup the dock for edit the current selection
*/
void QETDiagramEditor::setUpSelectionPropertiesEditor()
{
	m_selection_properties_editor = new DiagramPropertiesEditorDockWidget(this);
	m_selection_properties_editor -> setObjectName("diagram_properties_editor_dock_widget");
	addDockWidget(Qt::RightDockWidgetArea, m_selection_properties_editor);
}

/**
	@brief QETDiagramEditor::setUpAutonumberingWidget
	Setup the dock for AutoNumbering Selection
*/
void QETDiagramEditor::setUpAutonumberingWidget()
{
	m_autonumbering_dock = new AutoNumberingDockWidget(this);
	m_autonumbering_dock -> setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	m_autonumbering_dock -> setFeatures(
				QDockWidget::DockWidgetClosable
				|QDockWidget::DockWidgetMovable
				|QDockWidget::DockWidgetFloatable);
	addDockWidget(Qt::RightDockWidgetArea, m_autonumbering_dock);
}

/**
	@brief QETDiagramEditor::showPdmDialog
	Show the PDM (圖檔管理) dialog: Gitea-backed check-out / check-in panel.
	The dialog is created lazily on first use.
*/
void QETDiagramEditor::ensurePdmDialog()
{
	if (m_pdm_dialog) return;

	m_pdm_dialog = new PdmDialog(this);
	connect(m_pdm_dialog, &PdmDialog::requestOpenFile, this,
		[this](const QString &file_path) {
			openAndAddProject(file_path, false);
		});
	// 入庫前自動存檔(同步),免使用者手動 Cmd+S,git 才抓得到最新編輯。
	connect(m_pdm_dialog, &PdmDialog::requestSaveFile, this,
		[this](const QString &file_path) {
			if (ProjectView *pv = viewForFile(file_path))
				pv->save();
		});
	// 新增圖檔:取目前開啟的圖檔,存檔後把路徑交給對話框匯入圖庫
	connect(m_pdm_dialog, &PdmDialog::requestAddCurrentDrawing, this,
		[this]() {
			ProjectView *pv = currentProjectView();
			if (!pv || !pv->project()) {
				QET::QetMessageBox::warning(m_pdm_dialog,
					tr("新增圖檔"),
					tr("請先開啟要加入圖庫的圖檔。"));
				return;
			}
			pv->save();   // 未命名會跳另存;取消則路徑為空
			const QString path = pv->project()->filePath();
			if (!path.isEmpty())
				m_pdm_dialog->addDrawingFromFile(path);
		});
	// 入庫/取消出庫後強制關閉該檔(不提示存檔:磁碟上已 commit 的
	// 版本才是正本),避免編輯器留著舊內容、下次出庫開到舊版。
	connect(m_pdm_dialog, &PdmDialog::requestCloseFile, this,
		[this](const QString &file_path) {
			ProjectView *pv = viewForFile(file_path);
			if (!pv) return;
			if (QETProject *proj = pv->project()) {
				proj->undoStack()->setClean();
				proj->setModified(false);
			}
			closeProject(pv);
		});
	// 背景連線結果決定工具列「圖檔管理」按鈕啟用與否:連上並取回資料才啟用。
	connect(m_pdm_dialog, &PdmDialog::connectionReady, this,
		[this](bool ok) {
			if (m_pdm_action) m_pdm_action->setEnabled(ok);
		});
	applyInterfaceFonts();
}

/**
	程式啟動後在背景先試連一次:先禁用「圖檔管理」按鈕,連上並取回資料後
	由 connectionReady 啟用;連不上就維持禁用(也省去開視窗才連線的等待)。
*/
void QETDiagramEditor::setUpPdmBackgroundConnect()
{
	if (m_pdm_action) m_pdm_action->setEnabled(false);
	ensurePdmDialog();
	m_pdm_dialog->startBackgroundConnect();
}

void QETDiagramEditor::showPdmDialog()
{
	ensurePdmDialog();
	m_pdm_dialog->show();
	m_pdm_dialog->raise();
	m_pdm_dialog->activateWindow();
}

void QETDiagramEditor::updatePdmToolbar()
{
	PdmDialog::OpenContext ctx = PdmDialog::NotManaged;
	if (m_pdm_dialog)
		if (ProjectView *pv = currentProjectView())
			if (QETProject *proj = pv->project())
				ctx = m_pdm_dialog->openContext(proj->filePath());

	const bool checkout = (ctx == PdmDialog::CheckoutEdit);
	const bool review   = (ctx == PdmDialog::ReviewReadOnly);

	// 出庫編輯改走入庫、審核檢視為唯讀:兩者都隱藏儲存/另存
	const bool managed = checkout || review;
	if (m_save_file)    m_save_file->setVisible(!managed);
	if (m_save_file_as) m_save_file_as->setVisible(!managed);
	if (m_pdm_checkin)  m_pdm_checkin->setVisible(checkout);
	if (m_pdm_cancel)   m_pdm_cancel->setVisible(checkout);

	// 審核檢視的檔:顯示確認完畢/核准發行,依角色決定可否按
	if (m_pdm_confirm) {
		m_pdm_confirm->setVisible(review);
		m_pdm_confirm->setEnabled(review && m_pdm_dialog
					  && m_pdm_dialog->isConfirmer());
	}
	if (m_pdm_release) {
		m_pdm_release->setVisible(review);
		m_pdm_release->setEnabled(review && m_pdm_dialog
					  && m_pdm_dialog->isReleaser());
	}
}

/**
	@brief QETDiagramEditor::applyInterfaceFonts
	Apply the per-region interface font sizes from the settings to the menu bar,
	tool bars and dock panels. Each region falls back to the global UI font size
	when its own key is unset. Call after the menus/toolbars/docks are built.
*/
void QETDiagramEditor::applyInterfaceFonts()
{
	QSettings settings;
	const int base = qApp->font().pointSize() > 0 ? qApp->font().pointSize() : 9;
	auto regionFont = [&](const QString &key) {
		QFont f = qApp->font();
		f.setPointSize(settings.value(key, base).toInt());
		return f;
	};

	const QFont menu_font = regionFont("fontsize_menu");
	if (menuBar()) {
		menuBar()->setFont(menu_font);
	}
	// The drop-down QMenus are top-level popups parented to the window, not to
	// the menu bar, so they don't inherit the menu-bar font — set them directly.
	const QList<QMenu *> menus = findChildren<QMenu *>();
	for (QMenu *m : menus) {
		m->setFont(menu_font);
	}
	const QFont toolbar_font = regionFont("fontsize_toolbar");
	// The text-under-icon captions (列印 / 匯出PDF / …) have their own dedicated
	// zone so they can be tuned independently of the rest of the toolbar font.
	// ToolBarBottomTextStyle draws the caption from each tool button's own font,
	// so set it on the buttons directly (after the toolbar font, so it wins).
	const QFont toolbar_icontext_font = regionFont("fontsize_toolbar_icontext");
	const QList<QToolBar *> toolbars = findChildren<QToolBar *>();
	for (QToolBar *tb : toolbars) {
		tb->setFont(toolbar_font);
		const QList<QToolButton *> buttons = tb->findChildren<QToolButton *>();
		for (QToolButton *btn : buttons) {
			btn->setFont(toolbar_icontext_font);
		}
	}

	// A QDockWidget::setFont only restyles its own title bar; the content widgets
	// (tree views, filter fields, …) do NOT inherit it, so apply the font to the
	// dock, its content widget and every descendant explicitly.
	auto applyToDock = [&](QDockWidget *dock, const QString &key) {
		if (!dock) return;
		const QFont f = regionFont(key);
		dock->setFont(f);
		if (QWidget *content = dock->widget()) {
			content->setFont(f);
			const QList<QWidget *> kids = content->findChildren<QWidget *>();
			for (QWidget *k : kids) {
				k->setFont(f);
			}
		}
	};

	applyToDock(qdw_pa, "fontsize_projectpanel");
	applyToDock(m_selection_properties_editor, "fontsize_properties");
	applyToDock(m_autonumbering_dock, "fontsize_properties");

	// 圖檔管理彈出視窗沿用專案面板的字級設定
	if (m_pdm_dialog) {
		const QFont f = regionFont("fontsize_projectpanel");
		m_pdm_dialog->setFont(f);
		const QList<QWidget *> kids =
				m_pdm_dialog->findChildren<QWidget *>();
		for (QWidget *k : kids) {
			k->setFont(f);
		}
	}

	// The folio (page) tabs and the project title live in the central MDI area,
	// not in a dock, so they are not reached by applyToDock. Make them follow the
	// project-panel zone size as well. The MDI sub-window font drives the title
	// bar ("檔案 « … »"); the ProjectView font propagates to the page tabs.
	const QFont project_panel_font = regionFont("fontsize_projectpanel");
	foreach (ProjectView *pv, openedProjects()) {
		pv->applyTabFont(project_panel_font);
		if (QMdiSubWindow *sub = subWindowForWidget(pv)) {
			sub->setFont(project_panel_font);
		}
	}

	// The collection panel needs its grid geometry recomputed after a font change,
	// so it exposes a dedicated method rather than the generic descendant sweep.
	if (m_qdw_elmt_collection) {
		m_qdw_elmt_collection->setFont(regionFont("fontsize_library"));
	}
	if (m_element_collection_widget) {
		m_element_collection_widget->applyLibraryFont(regionFont("fontsize_library"));
	}
}

/**
	@brief QETDiagramEditor::setUpActions
	Set up all Qaction
*/
void QETDiagramEditor::setUpActions()
{
		//Export to another file type (jpeg, dxf etc...)
	m_export_to_images = new QAction(QET::Icons::DocumentExport,  tr("E&xporter"), this);
	m_export_to_images->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_X);
	m_export_to_images->setStatusTip(tr("Exporte le folio courant dans un autre format", "status bar tip"));
	connect(m_export_to_images, &QAction::triggered, [this]() {
		ProjectView *current_project = currentProjectView();
		if (current_project) {
			current_project -> exportProject();
		}
	});

		//Print
	m_print = new QAction(QET::Icons::DocumentPrint,   tr("Imprimer"),  this);
	m_print->setShortcut(QKeySequence::Print);
	m_print->setStatusTip(tr("Imprime un ou plusieurs folios du projet courant", "status bar tip"));
	connect(m_print, &QAction::triggered, [this]() {
		auto project = currentProject();
		if (project) {
			ProjectPrintWindow::launchDialog(project, QPrinter::NativeFormat ,this);
		}
	});

		//export to pdf
	m_export_to_pdf = new QAction(QET::Icons::PDF, tr("Exporter en pdf"), this);
	m_export_to_pdf->setStatusTip(tr("Exporte un ou plusieurs folios du projet courant", "status bar tip"));
	connect(m_export_to_pdf, &QAction::triggered, [this] () {
		auto project = currentProject();
		if (project) {
			ProjectPrintWindow::launchDialog(project, QPrinter::PdfFormat, this);
		}
	});

		//圖檔管理(PDM)
	m_pdm_action = new QAction(QET::Icons::ObjectLocked, tr("圖檔管理"), this);
	m_pdm_action->setStatusTip(tr("開啟圖檔管理視窗(出庫/入庫/送審/發行)"));
	connect(m_pdm_action, &QAction::triggered,
		this, &QETDiagramEditor::showPdmDialog);

	// 依開檔情境出現的圖檔管理工具列動作(對「目前作用中專案的檔」操作)
	auto active_qet_path = [this]() -> QString {
		if (ProjectView *pv = currentProjectView())
			if (pv->project()) return pv->project()->filePath();
		return QString();
	};
	m_pdm_checkin = new QAction(QET::Icons::DocumentSave, tr("入庫"), this);
	m_pdm_checkin->setStatusTip(tr("把目前圖檔入庫(存檔並提交到圖庫)"));
	connect(m_pdm_checkin, &QAction::triggered, this, [this, active_qet_path]() {
		ensurePdmDialog();
		m_pdm_dialog->checkInByPath(active_qet_path());
	});
	m_pdm_cancel = new QAction(QET::Icons::DocumentClose, tr("取消出庫"), this);
	m_pdm_cancel->setStatusTip(tr("捨棄未入庫修改並解除鎖定"));
	connect(m_pdm_cancel, &QAction::triggered, this, [this, active_qet_path]() {
		ensurePdmDialog();
		m_pdm_dialog->cancelByPath(active_qet_path());
	});
	m_pdm_confirm = new QAction(QET::Icons::PartSelect, tr("確認完畢"), this);
	m_pdm_confirm->setStatusTip(tr("確認者:確認完畢(寫確認者並記錄)"));
	connect(m_pdm_confirm, &QAction::triggered, this, [this, active_qet_path]() {
		ensurePdmDialog();
		m_pdm_dialog->confirmByPath(active_qet_path());
	});
	m_pdm_release = new QAction(QET::Icons::QETLogo, tr("核准發行"), this);
	m_pdm_release->setStatusTip(tr("核准者:核准並發行"));
	connect(m_pdm_release, &QAction::triggered, this, [this, active_qet_path]() {
		ensurePdmDialog();
		m_pdm_dialog->releaseByPath(active_qet_path());
	});

		//Quit editor
	m_quit_editor = new QAction(QET::Icons::ApplicationExit, tr("&Quitter"),  this);
	m_quit_editor->setShortcut(Qt::CTRL | Qt::Key_Q);
	m_quit_editor->setStatusTip(tr("Ferme l'application QElectroTech", "status bar tip"));
	connect(m_quit_editor, &QAction::triggered, this, &QETDiagramEditor::close);

		//Undo
	undo = undo_group.createUndoAction(this, tr("Annuler"));
	undo->setIcon(QET::Icons::EditUndo);
	undo->setShortcut(QKeySequence::Undo);
	undo->setStatusTip(tr("Annule l'action précédente", "status bar tip"));
		//Redo
	redo = undo_group.createRedoAction(this, tr("Refaire"));
	redo->setIcon(QET::Icons::EditRedo);
	redo->setShortcut(QKeySequence::Redo);
	redo->setStatusTip(tr("Restaure l'action annulée", "status bar tip"));

		//cut copy past
	m_cut   = new QAction(QET::Icons::EditCut,   tr("Co&uper"), this);
	m_copy  = new QAction(QET::Icons::EditCopy,  tr("Cop&ier"), this);
	m_paste = new QAction(QET::Icons::EditPaste, tr("C&oller"), this);

	m_cut   -> setShortcut(QKeySequence::Cut);
	m_copy  -> setShortcut(QKeySequence::Copy);
	m_paste -> setShortcut(QKeySequence::Paste);

	m_cut   -> setStatusTip(tr("Transfère les éléments sélectionnés dans le presse-papier", "status bar tip"));
	m_copy  -> setStatusTip(tr("Copie les éléments sélectionnés dans le presse-papier", "status bar tip"));
	m_paste -> setStatusTip(tr("Place les éléments du presse-papier sur le folio", "status bar tip"));

	connect(m_cut, &QAction::triggered, [this]() {
		if (currentDiagramView())
			currentDiagramView()->cut();
	});
	connect(m_copy, &QAction::triggered, [this]() {
		if (currentDiagramView())
			currentDiagramView()->copy();
	});
	connect(m_paste, &QAction::triggered, [this]() {
		if(currentDiagramView())
			currentDiagramView()->paste();
	});

		//Reset conductor path
	m_conductor_reset = new QAction(QET::Icons::ConductorSettings,     tr("Réinitialiser les conducteurs"),        this);
	m_conductor_reset->setShortcut(Qt::CTRL | Qt::Key_K);
	m_conductor_reset->setStatusTip(tr("Recalcule les chemins des conducteurs sans tenir compte des modifications", "status bar tip"));
	connect(m_conductor_reset, &QAction::triggered, [this]() {
		if (DiagramView *dv = currentDiagramView())
			dv->resetConductors();
	});

		//AutoConductor
	m_auto_conductor = new QAction   (QET::Icons::Autoconnect, tr("Création automatique de conducteur(s)","Tool tip of auto conductor"), this);
	m_auto_conductor->setStatusTip (tr("Utiliser la création automatique de conducteur(s) quand cela est possible", "Status tip of auto conductor"));
	m_auto_conductor->setCheckable (true);
	connect(m_auto_conductor, &QAction::triggered, [this](bool ac) {
		if (ProjectView *pv = currentProjectView())
			pv->project()->setAutoConductor(ac);
	});

		//Switch background color
		//Draw or not the background grid
	m_draw_grid = new QAction ( QET::Icons::Grid, tr("Afficher la grille"), this);
	m_draw_grid->setStatusTip(tr("Affiche ou masque la grille des folios"));
	m_draw_grid->setCheckable(true);
	m_draw_grid->setChecked(true);
	connect(m_draw_grid, &QAction::triggered, [this](bool checked) {
		foreach (ProjectView *prjv, this->openedProjects())
			foreach (Diagram *d, prjv->project()->diagrams()) {
				d->setDisplayGrid(checked);
				d->update();
			}
	});

		//Edit current diagram properties
	m_edit_diagram_properties = new QAction(QET::Icons::DialogInformation, tr("Propriétés du folio"), this);
	m_edit_diagram_properties->setShortcut(Qt::CTRL | Qt::Key_L);
	m_edit_diagram_properties     -> setStatusTip(tr("Édite les propriétés du folio (dimensions, informations du cartouche, propriétés des conducteurs...)", "status bar tip"));
	connect(m_edit_diagram_properties, &QAction::triggered, [this]() {
		if (ProjectView *project_view = currentProjectView())
		{
			activateProject(project_view);
			project_view->editCurrentDiagramProperties();
		}
	});

		//Edit current diagram revision history
	m_edit_folio_revisions = new QAction(QET::Icons::DocumentSpreadsheet, tr("Révisions du folio"), this);
	m_edit_folio_revisions->setStatusTip(tr("Édite l'historique des révisions du cartouche du folio courant", "status bar tip"));
	connect(m_edit_folio_revisions, &QAction::triggered, [this]() {
		if (ProjectView *project_view = currentProjectView())
		{
			activateProject(project_view);
			if (DiagramView *dv = project_view->currentDiagram()) {
				FolioRevisionsDialog::edit(dv->diagram(), this);
			}
		}
	});

		//Edit current project properties
	m_project_edit_properties = new QAction(QET::Icons::ProjectProperties, tr("Propriétés du projet"), this);
	connect(m_project_edit_properties, &QAction::triggered, [this]() {
		editProjectProperties(currentProjectView());
	});

		//Add new folio to current project
	m_project_add_diagram = new QAction(QET::Icons::DiagramAdd, tr("Ajouter un folio"), this);
	m_project_add_diagram->setShortcut(Qt::CTRL | Qt::Key_T);
	connect(m_project_add_diagram, &QAction::triggered, [this]() {
		if (ProjectView *current_project = currentProjectView()) {
			Diagram *diagram =
				current_project->project()->addNewDiagram();
			//ouvrir directement le cartouche du nouveau folio
			if (diagram) {
				editDiagramProperties(diagram);
			}
		}
	});

		//Remove current folio from current project
	m_remove_diagram_from_project = new QAction(QET::Icons::DiagramDelete, tr("Supprimer le folio"), this);
	connect(m_remove_diagram_from_project, &QAction::triggered, this, &QETDiagramEditor::removeDiagramFromProject);

		//Clean the current project
	m_clean_project         = new QAction(QET::Icons::EditClear,             tr("Nettoyer le projet"),                   this);
	connect(m_clean_project, &QAction::triggered, [this]() {
		if (ProjectView *current_project = currentProjectView()) {
			if (current_project->cleanProject()) {
				pa -> reloadAndFilter();
			}
		}
	});

		//Export nomenclature to CSV
	m_csv_export = new QAction(QET::Icons::DocumentSpreadsheet, tr("Exporter au format CSV"), this);
	connect(m_csv_export, &QAction::triggered, [this]() {
		BOMExportDialog bom(currentProjectView()->project(), this);
		bom.exec();
	});

		//Add a nomenclature item
	m_add_nomenclature = new QAction(QET::Icons::TableOfContent, tr("Ajouter une nomenclature"), this);
	connect(m_add_nomenclature, &QAction::triggered, this, [=]() {
		if(this->currentDiagramView()) {
			QetGraphicsTableFactory::createAndAddNomenclature(this->currentDiagramView()->diagram());
		}
	});

		//Add a summary item
	m_add_summary = new QAction(QET::Icons::TableOfContent, tr("Ajouter un sommaire"), this);
	connect(m_add_summary, &QAction::triggered, this, [=]() {
		if(this->currentDiagramView()) {
			QetGraphicsTableFactory::createAndAddSummary(this->currentDiagramView()->diagram());
		}
	});

	m_terminal_strip_dialog = new QAction(QET::Icons::TerminalStrip, tr("Gestionnaire de borniers (DEV)"), this);
	connect(m_terminal_strip_dialog, &QAction::triggered, this, [=]()
	{
		if (auto project = this->currentProject())
		{
			TerminalStripEditorWindow::instance(project, this)->show();
		}
	});

		//Launch the plugin of terminal generator
	m_project_terminalBloc = new QAction(QET::Icons::TerminalStrip, tr("Lancer le plugin de création de borniers"), this);
	connect(m_project_terminalBloc, &QAction::triggered, this, &QETDiagramEditor::generateTerminalBlock);

	//Export conductor num to csv
	m_project_export_conductor_num = new QAction(QET::Icons::DocumentSpreadsheet, tr("Exporter la liste des noms de conducteurs"), this);
	connect(m_project_export_conductor_num, &QAction::triggered, [this]() {
		QETProject *project = this->currentProject();
		if (project)
		{
			ConductorNumExport wne(project, this);
			wne.toCsv();
		}
	});
	// Export wiring list to CSV
	m_project_export_wiring_list = new QAction(QET::Icons::DocumentSpreadsheet, tr("Exporter le plan de câblage"), this);
	connect(m_project_export_wiring_list, &QAction::triggered, [this]() {
		QETProject *project = this->currentProject();
		if (project)
		{
			WiringListExport wle(project, this);
			wle.toCsv();
		}
	});

	// Terminal Numbering
	m_terminal_numbering = new QAction(QET::Icons::TerminalStrip, tr("Numérotation automatique des bornes"), this);
	connect(m_terminal_numbering, &QAction::triggered, this, &QETDiagramEditor::slot_terminalNumbering);

	#ifdef QET_EXPORT_PROJECT_DB
		m_export_project_db = new QAction(QET::Icons::DocumentSpreadsheet, tr("Exporter la base de donnée interne du projet"), this);
		connect(m_export_project_db, &QAction::triggered, [this]() {
			projectDataBase::exportDb(this->currentProject()->dataBase(), this);
		});
	#endif

		//MDI view style
	m_tabbed_view_mode = new QAction(tr("en utilisant des onglets"), this);
	m_tabbed_view_mode->setStatusTip(tr("Présente les différents projets ouverts des onglets", "status bar tip"));
	m_tabbed_view_mode->setCheckable(true);
	connect(m_tabbed_view_mode, &QAction::triggered, this, &QETDiagramEditor::setTabbedMode);

	m_windowed_view_mode = new QAction(tr("en utilisant des fenêtres"), this);
	m_windowed_view_mode->setStatusTip(tr("Présente les différents projets ouverts dans des sous-fenêtres", "status bar tip"));
	m_windowed_view_mode->setCheckable(true);
	connect(m_windowed_view_mode, &QAction::triggered, this, &QETDiagramEditor::setWindowedMode);

	m_group_view_mode = new QActionGroup(this);
	m_group_view_mode -> addAction(m_windowed_view_mode);
	m_group_view_mode -> addAction(m_tabbed_view_mode);
	m_group_view_mode -> setExclusive(true);

	m_tile_window = new QAction(tr("&Mosaïque"), this);
	m_tile_window->setStatusTip(tr("Dispose les fenêtres en mosaïque", "status bar tip"));
	connect(m_tile_window, &QAction::triggered, &m_workspace, &QMdiArea::tileSubWindows);

	m_cascade_window = new QAction(tr("&Cascade"), this);
	m_cascade_window->setStatusTip(tr("Dispose les fenêtres en cascade", "status bar tip"));
	connect(m_cascade_window, &QAction::triggered, &m_workspace, &QMdiArea::cascadeSubWindows);

		//Switch selection/view mode
	m_mode_selection = new QAction(QET::Icons::PartSelect, tr("Mode Selection"), this);
	m_mode_selection->setStatusTip(tr("Permet de sélectionner les éléments", "status bar tip"));
	m_mode_selection->setCheckable(true);
	m_mode_selection->setChecked(true);
	connect(m_mode_selection, &QAction::triggered, [this]() {
		if (ProjectView *pv = currentProjectView()) {
			for (DiagramView *dv : pv->diagram_views()) {
				dv->setSelectionMode();
			}
		}
	});

	m_mode_visualise = new QAction(QET::Icons::ViewMove, tr("Mode Visualisation"), this);
	m_mode_visualise->setStatusTip(tr("Permet de visualiser le folio sans pouvoir le modifier", "status bar tip"));
	m_mode_visualise->setCheckable(true);
	connect(m_mode_visualise, &QAction::triggered, [this]() {
		if (ProjectView *pv = currentProjectView()) {
			for(DiagramView *dv : pv->diagram_views()) {
				dv->setVisualisationMode();
			}
		}
	});

	grp_visu_sel = new QActionGroup(this);
	grp_visu_sel->addAction(m_mode_selection);
	grp_visu_sel->addAction(m_mode_visualise);
	grp_visu_sel->setExclusive(true);

		//Navigate next/previous project
	m_next_window = new QAction(tr("Projet suivant"), this);
	m_next_window->setShortcut(QKeySequence::NextChild);
	m_next_window->setStatusTip(tr("Active le projet suivant", "status bar tip"));
	connect(m_next_window, &QAction::triggered, &m_workspace, &QMdiArea::activateNextSubWindow);

	m_previous_window = new QAction(tr("Projet précédent"), this);
	m_previous_window->setShortcut(QKeySequence::PreviousChild);
	m_previous_window->setStatusTip(tr("Active le projet précédent", "status bar tip"));
	connect(m_previous_window, &QAction::triggered, &m_workspace, &QMdiArea::activatePreviousSubWindow);

		//Files action
	QAction *new_file  = m_file_actions_group.addAction(QET::Icons::ProjectNew,     tr("&Nouveau"));
	QAction *open_file = m_file_actions_group.addAction(QET::Icons::DocumentOpen,   tr("&Ouvrir"));
	m_save_file        = m_file_actions_group.addAction(QET::Icons::DocumentSave,   tr("&Enregistrer"));
	m_save_file_as     = m_file_actions_group.addAction(QET::Icons::DocumentSaveAs, tr("Enregistrer sous"));
	m_close_file       = m_file_actions_group.addAction(QET::Icons::ProjectClose,   tr("&Fermer"));

	new_file     ->setShortcut(QKeySequence::New);
	open_file    ->setShortcut(QKeySequence::Open);
	m_close_file ->setShortcut(QKeySequence::Close);
	m_save_file    ->setShortcut(QKeySequence::Save);
	m_save_file_as  ->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_S);

	new_file     ->setStatusTip( tr("Crée un nouveau projet", "status bar tip") );
	open_file    ->setStatusTip( tr("Ouvre un projet existant", "status bar tip") );
	m_close_file ->setStatusTip( tr("Ferme le projet courant", "status bar tip") );
	m_save_file    ->setStatusTip( tr("Enregistre le projet courant et tous ses folios", "status bar tip") );
	m_save_file_as ->setStatusTip( tr("Enregistre le projet courant avec un autre nom de fichier", "status bar tip") );

	connect(m_save_file_as, &QAction::triggered, this, &QETDiagramEditor::saveAs);
	connect(m_save_file,    &QAction::triggered, this, &QETDiagramEditor::save);
	connect(new_file,       &QAction::triggered, this, &QETDiagramEditor::newProject);
	connect(open_file,      &QAction::triggered, this, &QETDiagramEditor::openProject);
	connect(m_close_file,   &QAction::triggered, [this]() {
		if (ProjectView *project_view = currentProjectView()) {
			closeProject(project_view);
		}
	});

		//Rows and Columns
	QAction *add_column    = m_row_column_actions_group.addAction( QET::Icons::EditTableInsertColumnRight, tr("Ajouter une colonne") );
	QAction *remove_column = m_row_column_actions_group.addAction( QET::Icons::EditTableDeleteColumn,      tr("Enlever une colonne") );
	QAction *add_row       = m_row_column_actions_group.addAction( QET::Icons::EditTableInsertRowUnder,    tr("Ajouter une ligne", "Add row") );
	QAction *remove_row    = m_row_column_actions_group.addAction( QET::Icons::EditTableDeleteRow,         tr("Enlever une ligne","Remove row") );

	add_column    -> setStatusTip( tr("Ajoute une colonne au folio", "status bar tip"));
	remove_column -> setStatusTip( tr("Enlève une colonne au folio", "status bar tip"));
	add_row       -> setStatusTip( tr("Agrandit le folio en hauteur", "status bar tip"));
	remove_row    -> setStatusTip( tr("Rétrécit le folio en hauteur", "status bar tip"));

	add_column   ->setData("add_column");
	remove_column->setData("remove_column");
	add_row      ->setData("add_row");
	remove_row   ->setData("remove_row");

	connect(&m_row_column_actions_group, &QActionGroup::triggered, this, &QETDiagramEditor::rowColumnGroupTriggered);

		//Selections Actions (related to a selected item)
	m_delete_selection     = m_selection_actions_group.addAction( QET::Icons::EditDelete,        tr("Supprimer")                 );
	m_rotate_selection     = m_selection_actions_group.addAction( QET::Icons::TransformRotate,   tr("Pivoter")                   );
	m_rotate_texts         = m_selection_actions_group.addAction( QET::Icons::ObjectRotateRight, tr("Orienter les textes")       );
	m_find_element         = m_selection_actions_group.addAction( QET::Icons::ZoomDraw,          tr("Retrouver dans le panel")   );
	m_edit_selection       = m_selection_actions_group.addAction( QET::Icons::ElementEdit,       tr("Éditer l'item sélectionné") );
	m_group_selected_texts = m_selection_actions_group.addAction( QET::Icons::textGroup,         tr("Grouper les textes sélectionnés"));

	m_delete_selection->setShortcut(Qt::Key_Delete);
	m_rotate_selection->setShortcut(Qt::Key_Space);
	m_rotate_texts    ->setShortcut(Qt::CTRL | Qt::Key_Space);
	m_edit_selection  ->setShortcut(Qt::CTRL | Qt::Key_E);

	m_delete_selection->setStatusTip( tr("Enlève les éléments sélectionnés du folio", "status bar tip"));
	m_rotate_selection->setStatusTip( tr("Pivote les éléments et textes sélectionnés", "status bar tip"));
	m_rotate_texts    ->setStatusTip( tr("Pivote les textes sélectionnés à un angle précis", "status bar tip"));
	m_find_element    ->setStatusTip( tr("Retrouve l'élément sélectionné dans le panel", "status bar tip"));

	m_delete_selection    ->setData("delete_selection");
	m_rotate_selection    ->setData("rotate_selection");
	m_rotate_texts        ->setData("rotate_selected_text");
	m_find_element        ->setData("find_selected_element");
	m_edit_selection      ->setData("edit_selected_element");
	m_group_selected_texts->setData("group_selected_texts");

	connect(&m_selection_actions_group, &QActionGroup::triggered, this, &QETDiagramEditor::selectionGroupTriggered);

		//Select Action
	QAction *select_all     = m_select_actions_group.addAction( QET::Icons::EditSelectAll,      tr("Tout sélectionner") );
	QAction *select_nothing = m_select_actions_group.addAction( QET::Icons::EditSelectNone,     tr("Désélectionner tout") );
	QAction *select_invert  = m_select_actions_group.addAction( QET::Icons::EditSelectInvert,   tr("Inverser la sélection") );

	select_all    ->setShortcut(QKeySequence::SelectAll);
	select_nothing->setShortcut(QKeySequence::Deselect);
	select_invert ->setShortcut(Qt::CTRL | Qt::Key_I);

	select_all    ->setStatusTip( tr("Sélectionne tous les éléments du folio", "status bar tip") );
	select_nothing->setStatusTip( tr("Désélectionne tous les éléments du folio", "status bar tip") );
	select_invert ->setStatusTip( tr("Désélectionne les éléments sélectionnés et sélectionne les éléments non sélectionnés", "status bar tip") );

	select_all    ->setData("select_all");
	select_nothing->setData("deselect");
	select_invert ->setData("invert_selection");

	connect(&m_select_actions_group, &QActionGroup::triggered, this, &QETDiagramEditor::selectGroupTriggered);

		//Zoom actions
	QAction *zoom_in      = m_zoom_actions_group.addAction( QET::Icons::ZoomIn,       tr("Zoom avant"));
	QAction *zoom_out     = m_zoom_actions_group.addAction( QET::Icons::ZoomOut,      tr("Zoom arrière"));
	QAction *zoom_content = m_zoom_actions_group.addAction( QET::Icons::ZoomDraw,     tr("Zoom sur le contenu"));
	QAction *zoom_fit     = m_zoom_actions_group.addAction( QET::Icons::ZoomFitBest,  tr("Zoom adapté"));
	QAction *zoom_reset   = m_zoom_actions_group.addAction( QET::Icons::ZoomOriginal, tr("Pas de zoom"));
	//barre d'outils : seul le cadrage du folio (version societe)
	m_zoom_action_toolBar << zoom_fit;

	zoom_in     ->setShortcut(QKeySequence::ZoomIn);
	zoom_out    ->setShortcut(QKeySequence::ZoomOut);
	zoom_content->setShortcut(Qt::CTRL | Qt::Key_8);
	zoom_fit    ->setShortcut(Qt::CTRL | Qt::Key_9);
	zoom_reset  ->setShortcut(Qt::CTRL | Qt::Key_0);

	zoom_in     ->setStatusTip(tr("Agrandit le folio", "status bar tip"));
	zoom_out    ->setStatusTip(tr("Rétrécit le folio", "status bar tip"));
	zoom_content->setStatusTip(tr("Adapte le zoom de façon à afficher tout le contenu du folio indépendamment du cadre"));
	zoom_fit    ->setStatusTip(tr("Adapte le zoom exactement sur le cadre du folio", "status bar tip"));
	zoom_reset  ->setStatusTip(tr("Restaure le zoom par défaut", "status bar tip"));

	zoom_in     ->setData("zoom_in");
	zoom_out    ->setData("zoom_out");
	zoom_content->setData("zoom_content");
	zoom_fit    ->setData("zoom_fit");
	zoom_reset  ->setData("zoom_reset");

	connect(&m_zoom_actions_group, &QActionGroup::triggered, this, &QETDiagramEditor::zoomGroupTriggered);

		//Adding action (add text, image, shape...)
	QAction *add_text      = m_add_item_actions_group.addAction(QET::Icons::PartTextField, tr("Ajouter un champ de texte"));
	QAction *add_image	   = m_add_item_actions_group.addAction(QET::Icons::adding_image,  tr("Ajouter une image"));
	QAction *add_line	   = m_add_item_actions_group.addAction(QET::Icons::PartLine,      tr("Ajouter une ligne", "Draw line"));
	QAction *add_rectangle = m_add_item_actions_group.addAction(QET::Icons::PartRectangle, tr("Ajouter un rectangle"));
	QAction *add_ellipse   = m_add_item_actions_group.addAction(QET::Icons::PartEllipse,   tr("Ajouter une ellipse"));
	QAction *add_polyline  = m_add_item_actions_group.addAction(QET::Icons::PartPolygon,   tr("Ajouter une polyligne"));

	add_text     ->setStatusTip(tr("Ajoute un champ de texte sur le folio actuel"));
	add_image    ->setStatusTip(tr("Ajoute une image sur le folio actuel"));
	add_line     ->setStatusTip(tr("Ajoute une ligne sur le folio actuel"));
	add_rectangle->setStatusTip(tr("Ajoute un rectangle sur le folio actuel"));
	add_ellipse  ->setStatusTip(tr("Ajoute une ellipse sur le folio actuel"));
	add_polyline ->setStatusTip(tr("Ajoute une polyligne sur le folio actuel"));

	add_text     ->setData(QStringLiteral("text"));
	add_image    ->setData(QStringLiteral("image"));
	add_line     ->setData(QStringLiteral("line"));
	add_rectangle->setData(QStringLiteral("rectangle"));
	add_ellipse  ->setData(QStringLiteral("ellipse"));
	add_polyline ->setData(QStringLiteral("polyline"));

	add_text->setCheckable(true);
	add_line->setCheckable(true);
	add_rectangle->setCheckable(true);
	add_ellipse->setCheckable(true);
	add_polyline->setCheckable(true);

	connect(&m_add_item_actions_group, &QActionGroup::triggered, this, &QETDiagramEditor::addItemGroupTriggered);

		//Depth action
	m_depth_action_group = QET::depthActionGroup(this);
	m_depth_action_group->setDisabled(true);

	connect(m_depth_action_group, &QActionGroup::triggered, [this](QAction *action) {
		this->currentDiagramView()->diagram()->changeZValue(action->data().value<QET::DepthOption>());
	});

	m_find = new QAction(tr("Chercher/remplacer"), this);
	m_find->setShortcut(QKeySequence::Find);
	connect(m_find, &QAction::triggered, [this]()
	{
		if (auto animator = m_search_and_replace_widget.findChild<QWidgetAnimation *>("search and replace animator")) {
			animator->setHidden(!m_search_and_replace_widget.isHidden());
		} else {
			this->m_search_and_replace_widget.setHidden(!m_search_and_replace_widget.isHidden());
		}
	});
}

/**
	@brief QETDiagramEditor::setUpToolBar
*/
void QETDiagramEditor::setUpToolBar()
{
	main_tool_bar = new QToolBar(tr("Outils"), this);
	main_tool_bar -> setObjectName("toolbar");

	edit_tool_bar = new QToolBar(tr("Édition"), this);
	edit_tool_bar -> setObjectName("edit_toolbar");

	view_tool_bar = new QToolBar(tr("Affichage"), this);
	view_tool_bar -> setObjectName("display");

	diagram_tool_bar = new QToolBar(tr("Schéma"), this);
	diagram_tool_bar -> setObjectName("diagram");

	main_tool_bar -> addActions(m_file_actions_group.actions());
	// 列印按鈕自工具列移除(仍保留「檔案」選單項與 ⌘P 快捷鍵)
	main_tool_bar -> addAction(m_export_to_pdf);
	main_tool_bar -> addSeparator();
	main_tool_bar -> addAction(m_pdm_action);
	// 圖檔管理右側:依開檔情境顯示的動作(預設隱藏,由 updatePdmToolbar 控制)
	main_tool_bar -> addAction(m_pdm_checkin);
	main_tool_bar -> addAction(m_pdm_cancel);
	main_tool_bar -> addAction(m_pdm_confirm);
	main_tool_bar -> addAction(m_pdm_release);
	// 編輯類動作獨立成 edit_tool_bar,方便無開圖/唯讀時整條隱藏
	edit_tool_bar -> addAction(m_project_add_diagram);
	edit_tool_bar -> addAction(m_remove_diagram_from_project);
	edit_tool_bar -> addSeparator();
	edit_tool_bar -> addAction(undo);
	edit_tool_bar -> addAction(redo);
	edit_tool_bar -> addSeparator();
	edit_tool_bar -> addAction(m_cut);
	edit_tool_bar -> addAction(m_copy);
	edit_tool_bar -> addAction(m_paste);
	edit_tool_bar -> addSeparator();
	edit_tool_bar -> addAction(m_delete_selection);
	edit_tool_bar -> addAction(m_rotate_selection);

	// Modes selection / visualisation et zoom
	view_tool_bar -> addAction(m_mode_selection);
	view_tool_bar -> addAction(m_mode_visualise);
	view_tool_bar -> addSeparator();
	view_tool_bar -> addSeparator();
	view_tool_bar -> addActions(m_zoom_action_toolBar);

	diagram_tool_bar -> addAction (m_edit_diagram_properties);
	diagram_tool_bar -> addAction (m_edit_folio_revisions);
	diagram_tool_bar -> addAction (m_conductor_reset);
	diagram_tool_bar -> addAction (m_auto_conductor);

	m_add_item_tool_bar = new QToolBar(tr("Ajouter"), this);
	m_add_item_tool_bar->setObjectName("adding");
	m_add_item_tool_bar->addActions(m_add_item_actions_group.actions());

	m_depth_tool_bar = new QToolBar(tr("Profondeur", "toolbar title"));
	m_depth_tool_bar->setObjectName("diagram_depth_toolbar");
	m_depth_tool_bar->addActions(m_depth_action_group->actions());

	/* libelles courts sous les icones (le texte complet reste dans
	 * les menus : seul iconText est redefini) */
	const auto set_icon_texts = [](const QList<QAction *> &actions,
				       const QStringList &texts) {
		for (int i = 0 ; i < actions.count() && i < texts.count() ; ++i) {
			actions.at(i)->setIconText(texts.at(i));
		}
	};
	set_icon_texts(m_file_actions_group.actions(),
		       { tr("Nouveau", "toolbar icon text"),
			 tr("Ouvrir", "toolbar icon text"),
			 tr("Enregistrer", "toolbar icon text"),
			 tr("Enregistrer sous", "toolbar icon text"),
			 tr("Fermer", "toolbar icon text") });
	set_icon_texts(m_zoom_action_toolBar,
		       { tr("Zoom folio", "toolbar icon text") });
	set_icon_texts(m_add_item_actions_group.actions(),
		       { tr("Texte", "toolbar icon text"),
			 tr("Image", "toolbar icon text"),
			 tr("Ligne", "toolbar icon text"),
			 tr("Rectangle", "toolbar icon text"),
			 tr("Ellipse", "toolbar icon text"),
			 tr("Polyligne", "toolbar icon text") });
	set_icon_texts(m_depth_action_group->actions(),
		       { tr("Premier plan", "toolbar icon text"),
			 tr("Rapprocher", "toolbar icon text"),
			 tr("Éloigner", "toolbar icon text"),
			 tr("Arrière plan", "toolbar icon text") });
	m_print->setIconText(tr("Imprimer", "toolbar icon text"));
	m_project_add_diagram->setIconText(
		tr("Ajouter un folio", "toolbar icon text"));
	m_remove_diagram_from_project->setIconText(
		tr("Supprimer le folio", "toolbar icon text"));
	m_export_to_pdf->setIconText(tr("Exporter en PDF", "toolbar icon text"));
	undo->setIconText(tr("Annuler", "toolbar icon text"));
	redo->setIconText(tr("Refaire", "toolbar icon text"));
	m_cut->setIconText(tr("Couper", "toolbar icon text"));
	m_copy->setIconText(tr("Copier", "toolbar icon text"));
	m_paste->setIconText(tr("Coller", "toolbar icon text"));
	m_delete_selection->setIconText(tr("Supprimer", "toolbar icon text"));
	m_rotate_selection->setIconText(tr("Pivoter", "toolbar icon text"));
	m_mode_selection->setIconText(tr("Sélection", "toolbar icon text"));
	m_mode_visualise->setIconText(tr("Visualisation", "toolbar icon text"));
	m_edit_diagram_properties->setIconText(
		tr("Cartouche", "toolbar icon text"));
	m_edit_folio_revisions->setIconText(
		tr("Révisions", "toolbar icon text"));
	m_conductor_reset->setIconText(tr("Recâblage", "toolbar icon text"));
	m_auto_conductor->setIconText(tr("Câblage auto", "toolbar icon text"));

	const QList<QToolBar *> top_toolbars {
		main_tool_bar, edit_tool_bar, view_tool_bar, diagram_tool_bar,
		m_add_item_tool_bar, m_depth_tool_bar };
	for (QToolBar *tool_bar : top_toolbars) {
		tool_bar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
		tool_bar->setIconSize(QSize(24, 24));
	}

	// 讓 text-under-icon 的文字在按鈕底部對齊：QET 圖示是 16/22px 混合 PNG，
	// 高度不一，QToolButton 預設把「圖示+文字」整塊置中，矮圖示的文字就偏上。
	// 用自訂樣式改成「圖示置中、文字固定貼底」，文字一律落在同一底線。
	QStyle *bottom_text_style = new ToolBarBottomTextStyle(this);
	for (QToolBar *tool_bar : top_toolbars) {
		tool_bar->setStyle(bottom_text_style);
	}

	addToolBar(Qt::TopToolBarArea, main_tool_bar);
	addToolBar(Qt::TopToolBarArea, edit_tool_bar);
	addToolBar(Qt::TopToolBarArea, view_tool_bar);
	addToolBar(Qt::TopToolBarArea, diagram_tool_bar);
	addToolBar(Qt::TopToolBarArea, m_add_item_tool_bar);
	addToolBar(Qt::TopToolBarArea, m_depth_tool_bar);
}

/**
	@brief QETDiagramEditor::setUpMenu
*/
void QETDiagramEditor::setUpMenu()
{

	QMenu* menu_fichier	  = new QMenu(tr("&Fichier"), this);
	QMenu* menu_edition	  = new QMenu(tr("&Édition"), this);
	QMenu* menu_project	  = new QMenu(tr("&Projet"), this);
	QMenu* menu_affichage = new QMenu(tr("Afficha&ge"), this);
	// QMenu *menu_outils    = new QMenu(tr("O&utils"), this);
	windows_menu = new QMenu(tr("Fe&nêtres"), this);

	insertMenu(settings_menu_, menu_fichier);
	insertMenu(settings_menu_, menu_edition);
	insertMenu(settings_menu_, menu_project);
	insertMenu(settings_menu_, menu_affichage);
	insertMenu(help_menu_, windows_menu);

	// File menu
	QMenu *recentfile = menu_fichier -> addMenu(QET::Icons::DocumentOpenRecent, tr("&Récemment ouverts"));
	recentfile->addActions(QETApp::projectsRecentFiles()->menu()->actions());
	connect(QETApp::projectsRecentFiles(), SIGNAL(fileOpeningRequested(const QString &)),
		this, SLOT(openRecentFile(const QString &)));
	menu_fichier -> addActions(m_file_actions_group.actions());
	menu_fichier -> addSeparator();
	//menu_fichier -> addAction(import_diagram);
	menu_fichier -> addAction(m_export_to_images);
	menu_fichier -> addAction(m_export_to_pdf);
	menu_fichier -> addAction(m_print);
	menu_fichier -> addSeparator();
	menu_fichier -> addAction(m_quit_editor);

	// menu Edition
	menu_edition -> addAction(undo);
	menu_edition -> addAction(redo);
	menu_edition -> addSeparator();
	menu_edition -> addAction(m_cut);
	menu_edition -> addAction(m_copy);
	menu_edition -> addAction(m_paste);
	menu_edition -> addSeparator();
	menu_edition -> addActions(m_select_actions_group.actions());
	menu_edition -> addSeparator();
	menu_edition -> addActions(m_selection_actions_group.actions());
	menu_edition -> addSeparator();
	menu_edition -> addAction(m_conductor_reset);
	menu_edition -> addSeparator();
	menu_edition -> addAction(m_edit_diagram_properties);
	menu_edition -> addAction(m_edit_folio_revisions);
	menu_edition -> addActions(m_row_column_actions_group.actions());
	menu_edition -> addSeparator();
	menu_edition -> addActions(m_depth_action_group->actions());
	menu_edition -> addSeparator();
	menu_edition -> addAction(m_find);

	// menu Projet
	menu_project -> addAction(m_project_edit_properties);
	menu_project -> addAction(m_project_add_diagram);
	menu_project -> addAction(m_remove_diagram_from_project);
	menu_project -> addAction(m_clean_project);
	menu_project -> addSeparator();
	menu_project -> addAction(m_add_summary);
	menu_project -> addAction(m_add_nomenclature);
	menu_project -> addAction(m_csv_export);
	menu_project -> addAction(m_project_export_conductor_num);
	menu_project -> addAction(m_terminal_strip_dialog);
	menu_project -> addAction(m_project_terminalBloc);
	menu_project -> addAction(m_project_export_wiring_list);
	menu_project -> addAction(m_terminal_numbering);
#ifdef QET_EXPORT_PROJECT_DB
	menu_project -> addSeparator();
	menu_project -> addAction(m_export_project_db);
#endif

	main_tool_bar         -> toggleViewAction() -> setStatusTip(tr("Affiche ou non la barre d'outils principale"));
	view_tool_bar         -> toggleViewAction() -> setStatusTip(tr("Affiche ou non la barre d'outils Affichage"));
	diagram_tool_bar      -> toggleViewAction() -> setStatusTip(tr("Affiche ou non la barre d'outils Schéma"));
	qdw_pa           -> toggleViewAction() -> setStatusTip(tr("Affiche ou non le panel d'appareils"));
	qdw_undo         -> toggleViewAction() -> setStatusTip(tr("Affiche ou non la liste des modifications"));


	// menu Affichage
	QMenu *projects_view_mode = menu_affichage -> addMenu(QET::Icons::ConfigureToolbars, tr("Afficher les projets"));
	projects_view_mode -> setTearOffEnabled(true);
	projects_view_mode -> addAction(m_windowed_view_mode);
	projects_view_mode -> addAction(m_tabbed_view_mode);

	menu_affichage -> addSeparator();
	menu_affichage -> addAction(m_mode_selection);
	menu_affichage -> addAction(m_mode_visualise);
	menu_affichage -> addSeparator();
	menu_affichage -> addAction(m_draw_grid);
	menu_affichage -> addSeparator();
	menu_affichage -> addActions(m_zoom_actions_group.actions());

	// menu Fenetres
	slot_updateWindowsMenu();
}

/**
	Permet de quitter l'application lors de la fermeture de la fenetre principale
	@param qce Le QCloseEvent correspondant a l'evenement de fermeture
*/
void QETDiagramEditor::closeEvent(QCloseEvent *qce)
{
	// quitte directement s'il n'y a aucun projet ouvert
	bool can_quit = true;
	if (openedProjects().count()) {
		// s'assure que la fenetre soit visible s'il y a des projets a fermer
		if (!isVisible() || isMinimized()) {
			if (isMaximized()) showMaximized();
			else showNormal();
		}
		// sinon demande la permission de fermer chaque projet
		foreach(ProjectView *project, openedProjects()) {
			if (!closeProject(project)) {
				can_quit = false;
				qce -> ignore();
				break;
			}
		}
	}
	if (can_quit) {
		writeSettings();
		setAttribute(Qt::WA_DeleteOnClose);
		qce -> accept();
	}
}

/**
	@brief QETDiagramEditor::event
	Reimplemented to :
	-Load elements collection when WindowActivate.
	@param e
	@return
*/
bool QETDiagramEditor::event(QEvent *e)
{
	if (m_first_show && e->type() == QEvent::WindowActivate)
	{
		m_first_show = false;
		QTimer::singleShot(250, m_element_collection_widget, SLOT(reload()));
		//au demarrage, montrer le dock projets plutot que l'onglet
		//restaure par restoreState (souvent les proprietes, vides)
		if (qdw_pa->isVisible() && !tabifiedDockWidgets(qdw_pa).isEmpty())
			qdw_pa->raise();
		// restoreState 之後、且工具列按鈕已實體化,才能正確隱藏編輯類圖示
		//(建構期呼叫時 widgetForAction 尚為 null,且會被 restoreState 蓋掉)
		bool read_only = false;
		if (ProjectView *pv = currentProjectView())
			if (QETProject *proj = pv->project())
				read_only = proj->isReadOnly()
					    && !proj->filePath().isEmpty();
		applyReadOnlyView(read_only);
		updatePdmToolbar();
	}
	return(QETMainWindow::event(e));
}

/**
	@brief QETDiagramEditor::save
	Ask the current active project to save
*/
void QETDiagramEditor::save()
{
	if (ProjectView *project_view = currentProjectView()) {
		QETResult saved = project_view -> save();

		if (saved.isOk()) {
			//save_file -> setDisabled(true);
			QETApp::projectsRecentFiles() -> fileWasOpened(project_view -> project() -> filePath());

			QString title = (project_view -> project() -> title ());
			if (title.isEmpty()) title = "QElectroTech ";
			QString filePath = (project_view -> project() -> filePath ());
			statusBar()-> showMessage(tr("Projet %1 enregistré dans le repertoire: %2.").arg(title).arg (filePath), 2000);
			m_element_collection_widget->highlightUnusedElement();
		}
		else {
			showError(saved);
		}
	}
}

/**
	@brief QETDiagramEditor::saveAs
	Ask the current active project to save as
*/
void QETDiagramEditor::saveAs()
{
	if (ProjectView *project_view = currentProjectView()) {
		QETResult save_file = project_view -> saveAs();
		if (save_file.isOk()) {
			QETApp::projectsRecentFiles() -> fileWasOpened(project_view -> project() -> filePath());

			QString title = (project_view -> project() -> title ());
			if (title.isEmpty()) title = "QElectroTech ";
			QString filePath = (project_view -> project() -> filePath ());
			statusBar()->showMessage(tr("Projet %1 enregistré dans le repertoire: %2.").arg(title).arg (filePath), 2000);
			m_element_collection_widget->highlightUnusedElement();
		}
		else {
			showError(save_file);
		}
	}
}

/**
	@brief QETDiagramEditor::newProject
	Create a new project with an empty diagram
	@return
*/
bool QETDiagramEditor::newProject()
{
	/* modele de projet societe (synchronise par « 更新公司元件庫 ») :
	 * un nouveau projet est une copie sans titre du modele */
	const QDir template_dir(
		QETApp::dataDir() % QStringLiteral("/Project-Example"));
	const QStringList template_files = template_dir.entryList(
		{ QStringLiteral("*.qet") }, QDir::Files, QDir::Name);
	if (!template_files.isEmpty())
	{
		auto *project = new QETProject(
			template_dir.filePath(template_files.last()), this);
		if (project->state() == QETProject::Ok) {
			project->setFilePath(QString());
			return addProject(project);
		}
		delete project;
	}

	auto new_project = new QETProject(this);

	// add new diagram
	new_project -> addNewDiagram();

	return addProject(new_project);
}

/**
	Slot utilise pour ouvrir un fichier recent.
	Transfere filepath au slot openAndAddDiagram seulement si cet editeur est
	actif
	@param filepath Fichier a ouvrir
	@see openAndAddDiagram
*/
bool QETDiagramEditor::openRecentFile(const QString &filepath)
{
	// small hack to prevent all diagram editors from trying to topen the required
	// recent file at the same time
	if (qApp -> activeWindow() != this) return(false);
	return(openAndAddProject(filepath));
}

/**
	Cette fonction demande un nom de fichier a ouvrir a l'utilisateur
	@return true si l'ouverture a reussi, false sinon
*/
bool QETDiagramEditor::openProject()
{
	// demande un chemin de fichier a ouvrir a l'utilisateur
	QString filepath = QFileDialog::getOpenFileName(
		this,
		tr("Ouvrir un fichier"),
		open_dialog_dir.absolutePath(),
		tr("Projets QElectroTech (*.qet);;Fichiers XML (*.xml);;Tous les fichiers (*)")
	);
	if (filepath.isEmpty()) return(false);

	// retient le dossier contenant le dernier projet ouvert
	open_dialog_dir = QDir(filepath);

	// ouvre le fichier
	return(openAndAddProject(filepath));
}

/**
	Ferme un projet
	@param project_view Projet a fermer
	@return true si la fermeture du projet a reussi, false sinon
	Note : cette methode renvoie true si project est nul
*/
bool QETDiagramEditor::closeProject(ProjectView *project_view)
{
	if (project_view) {
		activateProject(project_view);
		if (QMdiSubWindow *sub_window = subWindowForWidget(project_view)){
			return(sub_window -> close());
		}
	}
	return(true);
}

/**
	Ferme un projet
	@param project projet a fermer
	@return true si la fermeture du fichier a reussi, false sinon
	Note : cette methode renvoie true si project est nul
*/
bool QETDiagramEditor::closeProject(QETProject *project)
{
	if (ProjectView *project_view = findProject(project)) {
		return(closeProject(project_view));
	}
	return(true);
}

/**
	Ouvre un projet depuis un fichier et l'ajoute a cet editeur
	@param filepath Chemin du projet a ouvrir
	@param interactive true pour afficher des messages a l'utilisateur, false sinon
	@return true si l'ouverture a reussi, false sinon
*/
bool QETDiagramEditor::openAndAddProject(
		const QString &filepath,
		bool interactive)
{
	if (filepath.isEmpty()) return(false);

	QFileInfo filepath_info(filepath);

	//Check if project is not open in another editor
	if (QETDiagramEditor *diagram_editor = QETApp::diagramEditorForFile(filepath))
	{
		if (diagram_editor == this)
		{
			if (ProjectView *project_view = viewForFile(filepath))
			{
				activateWidget(project_view);
				show();
				activateWindow();
			}
			return(false);
		}
		else
		{
				//Ask to the other editor to display the file
			return(diagram_editor -> openAndAddProject(filepath));
		}
	}

	// check the file exists
	if (!filepath_info.exists())
	{
		if (interactive)
		{
			QET::QetMessageBox::critical(
				this,
				tr("Impossible d'ouvrir le fichier", "message box title"),
				QString(
					tr("Il semblerait que le fichier %1 que vous essayez d'ouvrir"
					" n'existe pas ou plus.")
				).arg(filepath)
			);
		}
		return(false);
	}

	//Check if file readable
	if (!filepath_info.isReadable())
	{
		if (interactive) {
			QET::QetMessageBox::critical(
				this,
				tr("Impossible d'ouvrir le fichier", "message box title"),
				tr("Il semblerait que le fichier que vous essayez d'ouvrir ne "
				"soit pas accessible en lecture. Il est donc impossible de "
				"l'ouvrir. Veuillez vérifier les permissions du fichier.")
			);
		}
		return(false);
	}

	//Check if file is read only
	if (!filepath_info.isWritable())
	{
		if (interactive) {
			QET::QetMessageBox::warning(
				this,
				tr("Ouverture du projet en lecture seule", "message box title"),
				tr("Il semblerait que le projet que vous essayez d'ouvrir ne "
				"soit pas accessible en écriture. Il sera donc ouvert en "
				"lecture seule.")
			);
		}
	}

	//Create the project
	DialogWaiting::instance(this);

	QETProject *project = new QETProject(filepath);
	if (project -> state() != QETProject::Ok)
	{
		if (interactive && project -> state() != QETProject::FileOpenDiscard)
		{
			QET::QetMessageBox::warning(
				this,
				tr("Échec de l'ouverture du projet", "message box title"),
				QString(
					tr(
						"Il semblerait que le fichier %1 ne soit pas un fichier"
						" projet QElectroTech. Il ne peut donc être ouvert.",
						"message box content"
					)
				).arg(filepath)
			);
		}
		delete project;
		DialogWaiting::dropInstance();
		return(false);
	}

	QETApp::projectsRecentFiles() -> fileWasOpened(filepath);
	addProject(project);
	DialogWaiting::dropInstance();
	return true;
}

/**
	Ajoute un projetmoveDiagramUp(
	@param project projet a ajouter
	@param update_panel Whether the elements panel should be warned this
	project has been added. Defaults to true.
*/
bool QETDiagramEditor::addProject(QETProject *project, bool update_panel)
{
	// enregistre le projet
	QETApp::registerProject(project);

	// cree un ProjectView pour visualiser le projet
	ProjectView *project_view = new ProjectView(project);
	addProjectView(project_view);

	// Make the new project's folio tabs and title bar follow the "project panel"
	// font zone immediately (applyInterfaceFonts() only runs at startup / on pref
	// change, so a freshly opened project would otherwise use the default size).
	{
		QSettings settings;
		const int base = qApp->font().pointSize() > 0 ? qApp->font().pointSize() : 9;
		QFont tab_font = qApp->font();
		tab_font.setPointSize(settings.value("fontsize_projectpanel", base).toInt());
		project_view->applyTabFont(tab_font);
		if (QMdiSubWindow *sub = subWindowForWidget(project_view)) {
			sub->setFont(tab_font);
		}
	}

	undo_group.addStack(project -> undoStack());

	m_element_collection_widget->addProject(project);

	// met a jour le panel d'elements
	if (update_panel) {
		pa -> elementsPanel().projectWasOpened(project);
		if (currentDiagramView() != nullptr)
		m_autonumbering_dock->setProject(project, project_view);
	}

	return(true);
}

/**
	@return la liste des projets ouverts dans cette fenetre
*/
QList<ProjectView *> QETDiagramEditor::openedProjects() const
{
	QList<ProjectView *> result;
	QList<QMdiSubWindow *> window_list(m_workspace.subWindowList());
	foreach(QMdiSubWindow *window, window_list) {
		if (ProjectView *project_view = qobject_cast<ProjectView *>(window -> widget())) {
			result << project_view;
		}
	}
	return(result);
}

/**
	@return Le projet actuellement edite (= qui a le focus dans l'interface
	MDI) ou 0 s'il n'y en a pas
*/
ProjectView *QETDiagramEditor::currentProjectView() const
{
	QMdiSubWindow *current_window = m_workspace.activeSubWindow();
	if (!current_window) return(nullptr);

	QWidget *current_widget = current_window -> widget();
	if (!current_widget) return(nullptr);

	if (ProjectView *project_view = qobject_cast<ProjectView *>(current_widget)) {
		return(project_view);
	}
	return(nullptr);
}

/**
	@brief QETDiagramEditor::currentProject
	@return the current edited project.
	This function can return nullptr.
*/
QETProject *QETDiagramEditor::currentProject() const
{
	ProjectView *view = currentProjectView();
	if (view) {
		return view->project();
	}
	else {
		return nullptr;
	}
}

/**
	@return Le schema actuellement edite (= l'onglet ouvert dans le projet
	courant) ou 0 s'il n'y en a pas
*/
DiagramView *QETDiagramEditor::currentDiagramView() const
{
	if (ProjectView *project_view = currentProjectView()) {
		return(project_view -> currentDiagram());
	}
	return(nullptr);
}

/**
	@return the selected element in the current diagram view, or 0 if:
	  * no diagram is being viewed in this editor.
	  * no element is selected
	  * more than one element is selected
*/
Element *QETDiagramEditor::currentElement() const
{
	DiagramView *dv = currentDiagramView();
	if (!dv)
		return(nullptr);

	QList<Element *> selected_elements = DiagramContent(dv->diagram()).m_elements;
	if (selected_elements.count() != 1)
		return(nullptr);

	return(selected_elements.first());
}

/**
	Cette methode permet de retrouver le projet contenant un schema donne.
	@param diagram_view Schema dont il faut retrouver
	@return la vue sur le projet contenant ce schema ou 0 s'il n'y en a pas
*/
ProjectView *QETDiagramEditor::findProject(DiagramView *diagram_view) const
{
	foreach(ProjectView *project_view, openedProjects()) {
		if (project_view -> diagram_views().contains(diagram_view)) {
			return(project_view);
		}
	}
	return(nullptr);
}

/**
	Cette methode permet de retrouver le projet contenant un schema donne.
	@param diagram Schema dont il faut retrouver
	@return la vue sur le projet contenant ce schema ou 0 s'il n'y en a pas
*/
ProjectView *QETDiagramEditor::findProject(Diagram *diagram) const
{
	foreach(ProjectView *project_view, openedProjects()) {
		foreach(DiagramView *diagram_view, project_view -> diagram_views()) {
			if (diagram_view -> diagram() == diagram) {
				return(project_view);
			}
		}
	}
	return(nullptr);
}

/**
	@param project Projet dont il faut trouver la vue
	@return la vue du projet passe en parametre
*/
ProjectView *QETDiagramEditor::findProject(QETProject *project) const
{
	foreach(ProjectView *opened_project, openedProjects()) {
		if (opened_project -> project() == project) {
			return(opened_project);
		}
	}
	return(nullptr);
}

/**
	@param filepath Chemin de fichier d'un projet
	@return le ProjectView correspondant au chemin passe en parametre, ou 0 si
	celui-ci n'a pas ete trouve
*/
ProjectView *QETDiagramEditor::findProject(const QString &filepath) const
{
	foreach(ProjectView *opened_project, openedProjects()) {
		if (QETProject *project = opened_project -> project()) {
			if (project -> filePath() == filepath) {
				return(opened_project);
			}
		}
	}
	return(nullptr);
}

/**
	@param widget Widget a rechercher dans la zone MDI
	@return La sous-fenetre accueillant le widget passe en parametre, ou 0 si
	celui-ci n'a pas ete trouve.
*/
QMdiSubWindow *QETDiagramEditor::subWindowForWidget(QWidget *widget) const
{
	foreach(QMdiSubWindow *sub_window, m_workspace.subWindowList()) {
		if (sub_window -> widget() == widget) {
			return(sub_window);
		}
	}
	return(nullptr);
}

/**
	@param widget Widget a activer
*/
void QETDiagramEditor::activateWidget(QWidget *widget) {
	QMdiSubWindow *sub_window = subWindowForWidget(widget);
	if (sub_window) {
		m_workspace.setActiveSubWindow(sub_window);
	}
}

void QETDiagramEditor::zoomGroupTriggered(QAction *action)
{
	QString value = action->data().toString();
	DiagramView *dv = currentDiagramView();

	if (!dv || value.isEmpty()) return;

	if (value == "zoom_in")
		dv->zoom(1.15);
	else if (value == "zoom_out")
		dv->zoom(0.85);
	else if (value == "zoom_content")
		dv->zoomContent();
	else if (value == "zoom_fit")
		dv->zoomFit();
	else if (value == "zoom_reset")
		dv->zoomReset();
}

/**
	@brief QETDiagramEditor::selectGroupTriggered
	This slot is called when selection need to change.
	@param action : Action that describes what to do.
*/
void QETDiagramEditor::selectGroupTriggered(QAction *action)
{
	if (!currentDiagramView() || !currentDiagramView()->diagram())
		return;

	auto value = action->data().toString();
	if (value.isEmpty())
		return;

	auto diagram = currentDiagramView()->diagram();

	if (value == "select_all")
		diagram->selectAll();
	else if (value == "deselect")
		diagram->deselectAll();
	else if (value == "invert_selection")
		diagram->invertSelection();
}

/**
	@brief QETDiagramEditor::addItemGroupTriggered
	This slot is called when an item must be added to the current diagram,
	this slot use the DVEventInterface to add item
	@param action : Action that describe the item to add.
*/
void QETDiagramEditor::addItemGroupTriggered(QAction *action)
{
	QString value = action->data().toString();

	if (Q_UNLIKELY (!currentDiagramView() || !currentDiagramView()->diagram() || value.isEmpty())) return;

	Diagram *d = currentDiagramView()->diagram();
	DiagramEventInterface *diagram_event = nullptr;

	if (value == "line")
		diagram_event = new DiagramEventAddShape (d, QetShapeItem::Line);
	else if (value == "rectangle")
		diagram_event = new DiagramEventAddShape (d, QetShapeItem::Rectangle);
	else if (value == "ellipse")
		diagram_event = new DiagramEventAddShape (d, QetShapeItem::Ellipse);
	else if (value == "polyline")
	{
		diagram_event = new DiagramEventAddShape (d, QetShapeItem::Polygon);
		statusBar()-> showMessage(tr("Double-click pour terminer la forme, Click droit pour annuler le dernier point"));
		connect(diagram_event, &DiagramEventInterface::destroyed, [this]() {
		statusBar()->clearMessage();
		});
	}
	else if (value == "image")
	{
		DiagramEventAddImage *deai = new DiagramEventAddImage(d);
		if (deai->isNull())
		{
			delete deai;
			return;
		}
		else
			diagram_event = deai;
	}
	else if (value == "text")
	{
		diagram_event = new DiagramEventAddText(d);
	}
	else if (value == QLatin1String("terminal_strip"))
	{
		const auto diagram_view{currentDiagramView()};
		if (diagram_view)
		{
			AddTerminalStripItemDialog::openDialog(diagram_view->diagram(), this);
		}
	}

	if (diagram_event)
	{
		d->setEventInterface(diagram_event);
		connect(diagram_event, &DiagramEventInterface::destroyed, [action]() {action->setChecked(false);});
	}
}

/**
	@brief QETDiagramEditor::selectionGroupTriggered
	This slot is called when an action should be made on the current selection
	@param action : Action that describe the action to do.
*/
void QETDiagramEditor::selectionGroupTriggered(QAction *action)
{
	QString value = action->data().toString();
	DiagramView *dv = currentDiagramView();
	Diagram *diagram = dv->diagram();
	DiagramContent dc(diagram);

	if (!dv || value.isEmpty()) return;

        if (value == "delete_selection")
        {
            if (DeleteQGraphicsItemCommand::hasNonDeletableTerminal(dc)) {
                QET::QetMessageBox::information(this,
                                                tr("Suppression de borne impossible"),
                                                tr("La suppression ne peut être effectué car la selection "
												   "possède une ou plusieurs bornes ponté et/ou appartenant à une borne à niveau multiple.\n"
                                                   "Déponter et/ou supprimer les niveaux des bornes concerné "
                                                   "afin de pouvoir les supprimer"));
            } else {
                diagram->clearSelection();
                diagram->undoStack().push(new DeleteQGraphicsItemCommand(diagram, dc));
                dv->adjustSceneRect();
            }
        }
	else if (value == "rotate_selection")
	{
		RotateSelectionCommand *c = new RotateSelectionCommand(diagram);
		if(c->isValid())
			diagram->undoStack().push(c);
	}
	else if (value == "rotate_selected_text")
		diagram->undoStack().push(new RotateTextsCommand(diagram));
	else if (value == "find_selected_element" && currentElement())
		findElementInPanel(currentElement()->location());
	else if (value == "edit_selected_element")
		dv->editSelection();
	else if (value == "group_selected_texts")
	{
		QList<DynamicElementTextItem *> deti_list = dc.m_element_texts.values();
		if(deti_list.size() <= 1)
			return;

		diagram->undoStack().push(new AddTextsGroupCommand(deti_list.first()->parentElement(), tr("Groupe"), deti_list));
	}
}

void QETDiagramEditor::rowColumnGroupTriggered(QAction *action)
{
	QString value = action->data().toString();
	DiagramView *dv = currentDiagramView();

	if (!dv || value.isEmpty() || dv->diagram()->isReadOnly()) return;

	Diagram *d = dv->diagram();
	BorderProperties old_bp = d->border_and_titleblock.exportBorder();
	BorderProperties new_bp = d->border_and_titleblock.exportBorder();

	if (value == "add_column")
		new_bp.columns_count += 1;
	else if (value == "remove_column")
		new_bp.columns_count -= 1;
	else if (value == "add_row")
		new_bp.rows_count += 1;
	else if (value == "remove_row")
		new_bp.rows_count -= 1;

	d->undoStack().push(new ChangeBorderCommand(d, old_bp, new_bp));
}

/**
	@brief QETDiagramEditor::slot_updateActions
	Manage actions
*/
void QETDiagramEditor::slot_updateActions()
{
	DiagramView *dv = currentDiagramView();
	ProjectView *pv = currentProjectView();

	bool opened_project = pv;
	bool opened_diagram = dv;
	bool editable_project = (pv && !pv -> project() -> isReadOnly());

	m_close_file->                  setEnabled(opened_project);
	m_save_file->                   setEnabled(opened_project);
	m_save_file_as->                setEnabled(opened_project);
	m_rotate_texts->                setEnabled(editable_project);
	m_export_to_images->            setEnabled(opened_diagram);
	m_print->                       setEnabled(opened_diagram);
	m_export_to_pdf->               setEnabled(opened_diagram);
	m_edit_diagram_properties->     setEnabled(opened_diagram);
	m_edit_folio_revisions->        setEnabled(opened_diagram);
	m_zoom_actions_group.           setEnabled(opened_diagram);
	m_select_actions_group.         setEnabled(opened_diagram);
	m_add_item_actions_group.       setEnabled(editable_project);
	m_row_column_actions_group.     setEnabled(editable_project);
	m_draw_grid->                   setEnabled(opened_diagram);

		//Project menu
	m_project_edit_properties     -> setEnabled(opened_project);
	m_project_add_diagram         -> setEnabled(editable_project);
	m_remove_diagram_from_project -> setEnabled(editable_project);
	m_clean_project               -> setEnabled(editable_project);
	m_add_summary                 -> setEnabled(editable_project);
	m_add_nomenclature            -> setEnabled(editable_project);
	m_csv_export                  -> setEnabled(editable_project);
	m_project_export_conductor_num-> setEnabled(opened_project);
	m_terminal_strip_dialog       -> setEnabled(editable_project);
	m_project_export_wiring_list  -> setEnabled(opened_project);
	m_terminal_numbering          -> setEnabled(editable_project);
#ifdef QET_EXPORT_PROJECT_DB
	m_export_project_db           -> setEnabled(editable_project);
#endif
	m_project_terminalBloc        -> setEnabled(editable_project);


	slot_updateUndoStack();
	slot_updateModeActions();
	slot_updatePasteAction();
	slot_updateComplexActions();
	slot_updateAutoNumDock();
}

/**
	@brief QETDiagramEditor::slot_updateAutoNumDock
	Update Auto Num Dock Widget when changing Project
*/
void QETDiagramEditor::slot_updateAutoNumDock()
{
	if ( m_workspace.subWindowList().indexOf(m_workspace.activeSubWindow()) != activeSubWindowIndex) {
			activeSubWindowIndex = m_workspace.subWindowList().indexOf(m_workspace.activeSubWindow());
			if (currentProjectView() != nullptr && currentDiagramView() != nullptr) {
				m_autonumbering_dock->setProject(currentProjectView()->project(),currentProjectView());
			}
	}
}

/**
	@brief QETDiagramEditor::slot_updateUndoStack
	Update the undo stack view
*/
void QETDiagramEditor::slot_updateUndoStack()
{
	if(currentProjectView())
		undo_group.setActiveStack(currentProjectView()->project()->undoStack());
}

/**
	@brief QETDiagramEditor::slot_updateComplexActions
	Manage the actions that need some conditions to be enabled or not.
	This method does nothing if there is no project opened
*/
void QETDiagramEditor::slot_updateComplexActions()
{
	DiagramView *dv = currentDiagramView();
	if(!dv)
	{
		QList <QAction *> action_list;
		action_list << m_conductor_reset
			    << m_find_element
			    << m_cut
			    << m_copy
			    << m_delete_selection
			    << m_rotate_selection
			    << m_edit_selection
			    << m_group_selected_texts;
		for(QAction *action : action_list)
			action->setEnabled(false);

		return;
	}

	Diagram *diagram_ = dv->diagram();
	DiagramContent dc(diagram_);
	bool ro = diagram_->isReadOnly();


	//Number of selected conductors
	int selected_conductors_count = diagram_->selectedConductors().count();
	m_conductor_reset->setEnabled(!ro && selected_conductors_count);

	// number of selected elements
	int selected_elements_count = dc.count(DiagramContent::Elements);
	m_find_element->setEnabled(selected_elements_count == 1);

	//Actions that need items (elements, conductors, texts...) selected, to be enabled
	bool copiable_items  = dc.hasCopiableItems();
	bool deletable_items = dc.hasDeletableItems();
	m_cut              -> setEnabled(!ro && copiable_items);
	m_copy             -> setEnabled(copiable_items);
	m_delete_selection -> setEnabled(!ro && deletable_items);
	m_rotate_selection -> setEnabled(!ro && diagram_->canRotateSelection());

		//Action that need selected texts or texts group
	QList<DiagramTextItem *> texts = DiagramContent(diagram_).selectedTexts();
	QList<ElementTextItemGroup *> groups = DiagramContent(diagram_).selectedTextsGroup();
	int selected_texts = texts.count();
	int selected_conductor_texts   = 0;
	for(DiagramTextItem *dti : texts)
	{
		if(dti->type() == ConductorTextItem::Type)
			selected_conductor_texts++;
	}
	int selected_dynamic_elmt_text = 0;
	for(DiagramTextItem *dti : texts)
	{
		if(dti->type() == DynamicElementTextItem::Type)
			selected_dynamic_elmt_text++;
	}
	m_rotate_texts->setEnabled(!ro && (selected_texts || groups.size()));

	//Action that need only element text selected
	QList<DynamicElementTextItem *> deti_list = dc.m_element_texts.values();
	if(deti_list.size() > 1 && dc.count() == deti_list.count())
	{
		Element *elmt = deti_list.first()->parentElement();
		bool ok = true;
		for(DynamicElementTextItem *deti : deti_list)
		{
			if(elmt != deti->parentElement())
				ok = false;
		}
		m_group_selected_texts->setEnabled(!ro && ok);
	}
	else
		m_group_selected_texts->setDisabled(true);

	// actions need only one editable item
	int selected_image = dc.count(DiagramContent::Images);

	int selected_shape = dc.count(DiagramContent::Shapes);
	int selected_editable = selected_elements_count
			+ (selected_texts
			   - selected_conductor_texts
			   - selected_dynamic_elmt_text)
			+ selected_image
			+ selected_shape
			+ selected_conductors_count;

	if (selected_editable == 1)
	{
		m_edit_selection -> setEnabled(true);
		//edit element
		if (selected_elements_count)
		{
			m_edit_selection -> setText(tr("Éditer l'élement",
						       "edit element"));
			m_edit_selection -> setIcon(QET::Icons::ElementEdit);
		}
		//edit text field
		else if (selected_texts)
		{
			m_edit_selection -> setText(tr("Éditer le champ de texte",
						       "edit text field"));
			m_edit_selection -> setIcon(QET::Icons::EditText);
		}
		//edit image
		else if (selected_image)
		{
			m_edit_selection -> setText(tr("Éditer l'image",
						       "edit image"));
			m_edit_selection -> setIcon(QET::Icons::resize_image);
		}
		//edit conductor
		else if (selected_conductors_count)
		{
			m_edit_selection -> setText(tr("Éditer le conducteur",
						       "edit conductor"));
			m_edit_selection -> setIcon(QET::Icons::ConductorEdit);
		}
	}
	//not an editable item
	else
	{
		m_edit_selection -> setText(tr("Éditer l'objet sélectionné",
					       "edit selected item"));
		m_edit_selection -> setIcon(QET::Icons::ElementEdit);
		m_edit_selection -> setEnabled(false);
	}

	//Actions for edit Z value
	QList<QGraphicsItem *> list = dc.items(
				DiagramContent::SelectedOnly
				| DiagramContent::Elements
				| DiagramContent::Shapes
				| DiagramContent::Images);
	m_depth_action_group->setEnabled(list.isEmpty()? false : true);
}

/**
	@brief QETDiagramEditor::slot_updateModeActions
	Manage action who need an opened diagram or project to be updated
*/
void QETDiagramEditor::slot_updateModeActions()
{
	DiagramView *dv = currentDiagramView();

	if (!dv)
		grp_visu_sel -> setEnabled(false);
	else
	{
		switch((int)(dv -> dragMode()))
		{
			case QGraphicsView::NoDrag:
				grp_visu_sel -> setEnabled(false);
				break;
			case QGraphicsView::ScrollHandDrag:
				grp_visu_sel -> setEnabled(true);
				m_mode_visualise -> setChecked(true);
				break;
			case QGraphicsView::RubberBandDrag:
				grp_visu_sel -> setEnabled(true);
				m_mode_selection -> setChecked(true);
				break;
		}
	}

	if (ProjectView *pv = currentProjectView())
	{
		m_auto_conductor -> setEnabled (true);
		m_auto_conductor -> setChecked (pv -> project() -> autoConductor());
	}
	else
		m_auto_conductor -> setDisabled(true);
}

/**
	@brief QETDiagramEditor::slot_updatePasteAction
	Gere les actions ayant besoin du presse-papier
*/
void QETDiagramEditor::slot_updatePasteAction()
{
	DiagramView *dv = currentDiagramView();
	bool editable_diagram = (dv && !dv -> diagram() -> isReadOnly());

	// pour coller, il faut un schema ouvert et un schema dans le presse-papier
	m_paste -> setEnabled(editable_diagram && Diagram::clipboardMayContainDiagram());
}

/**
	@brief QETDiagramEditor::addProjectView
	Add a new project view to workspace and build the connection between
	the projectview / project and this QETDiagramEditor.
	@param project_view : project view to add
*/
void QETDiagramEditor::addProjectView(ProjectView *project_view)
{
	if (!project_view) return;

	foreach(DiagramView *dv, project_view -> diagram_views())
		diagramWasAdded(dv);

	//Manage the close event of project
	connect(project_view, SIGNAL(projectClosed(ProjectView*)),
		this, SLOT(projectWasClosed(ProjectView *)));
	//Manage the adding  of diagram
	connect(project_view, SIGNAL(diagramAdded(DiagramView *)),
		this, SLOT(diagramWasAdded(DiagramView *)));

	if (QETProject *project = project_view -> project())
		connect(project, SIGNAL(readOnlyChanged(QETProject *, bool)),
			this, SLOT(slot_updateActions()));

	//Manage request for edit or find element and titleblock
	connect (project_view, &ProjectView::findElementRequired,
		 this, &QETDiagramEditor::findElementInPanel);

	// display error messages sent by the project view
	connect(project_view, SIGNAL(errorEncountered(QString)),
		this, SLOT(showError(const QString &)));

	//Highlight the current page
	connect(project_view, &ProjectView::diagramActivated, this, [this](DiagramView *dv) {
		if (dv && dv->diagram() && pa) {
			// 1. Find the item in the tree that corresponds to this diagram
			QTreeWidgetItem *item = pa->elementsPanel().getItemForDiagram(dv->diagram());

				   // 2. If you find it, select it
			if (item) {
				pa->elementsPanel().setCurrentItem(item);
			}
		}
	});

		//Highlight the current page in projectView on project activation
	connect(this, &QETDiagramEditor::syncElementsPanel, this, [this]() {
		if (pa && currentDiagramView()) {
				// In the tree, find the element that corresponds to the diagram of the selected project.
			QTreeWidgetItem *item = pa->elementsPanel().getItemForDiagram(currentDiagramView()->diagram());
			if (item) {
					// select the diagram
				pa->elementsPanel().setCurrentItem(item);
			}
		}
	});

	//We maximise the new window if the current window is inexistent or maximized
	QWidget *current_window = m_workspace.activeSubWindow();
	bool maximise = ((!current_window)
			 || (current_window -> windowState()
			     & Qt::WindowMaximized));

		//Add the new window
	QMdiSubWindow *sub_window = m_workspace.addSubWindow(project_view);
	sub_window -> setWindowIcon(project_view -> windowIcon());
	sub_window -> systemMenu() -> clear();

	//By default QMdiSubWindow have a QAction "close" with shortcut QKeySequence::Close
	//But the QAction m_close_file of this class have the same shortcut too.
	//We remove the shortcut of the QAction of QMdiSubWindow for avoid conflic
	for(QAction *act : sub_window->actions())
	{
		if(act->shortcut() == QKeySequence::Close)
			act->setShortcut(QKeySequence());
	}

		//Display the new window
	if (maximise) project_view -> showMaximized();
	else          project_view -> show();
}

/**
	@return la liste des fichiers edites par cet editeur de schemas
*/
QList<QString> QETDiagramEditor::editedFiles() const
{
	QList<QString> edited_files_list;
	foreach (ProjectView *project_view, openedProjects()) {
		QString diagram_file(project_view -> project() -> filePath());
		if (!diagram_file.isEmpty()) {
			edited_files_list << QFileInfo(diagram_file).canonicalFilePath();
		}
	}
	return(edited_files_list);
}

/**
	@param filepath Un chemin de fichier
	Note : si filepath est une chaine vide, cette methode retourne 0.
	@return le ProjectView editant le fichier filepath, ou 0 si ce fichier n'est
	pas edite par cet editeur de schemas.
*/
ProjectView *QETDiagramEditor::viewForFile(const QString &filepath) const
{
	if (filepath.isEmpty()) return(nullptr);

	QString searched_can_file_path = QFileInfo(filepath).canonicalFilePath();
	if (searched_can_file_path.isEmpty()) {
		// QFileInfo returns an empty path for non-existent files
		return(nullptr);
	}
	foreach (ProjectView *project_view, openedProjects()) {
		QString project_can_file_path = QFileInfo(project_view -> project() -> filePath()).canonicalFilePath();
		if (project_can_file_path == searched_can_file_path) {
			return(project_view);
		}
	}
	return(nullptr);
}

/**
	@brief QETDiagramEditor::drawGrid
	@return true if the grid of folio must be displayed
*/
bool QETDiagramEditor::drawGrid() const
{
	return m_draw_grid->isChecked();
}

#ifdef BUILD_WITHOUT_KF5
#else
/**
	@brief QETDiagramEditor::openBackupFiles
	@param backup_files
*/
void QETDiagramEditor::openBackupFiles(QList<KAutoSaveFile *> backup_files)
{
	for (KAutoSaveFile *file : backup_files)
	{
			//Create the project
		DialogWaiting::instance(this);

		QETProject *project = new QETProject(file, this);
		if (project->state() != QETProject::Ok)
		{
			if (project -> state() != QETProject::FileOpenDiscard)
			{
				QET::QetMessageBox::warning(
					this,
					tr("Échec de l'ouverture du projet", "message box title"),
					QString(tr(
						"Une erreur est survenue lors de l'ouverture du fichier %1.",
						"message box content")).arg(file->managedFile().fileName()));
			}
			delete project;
			DialogWaiting::dropInstance();
		}
		addProject(project);
		DialogWaiting::dropInstance();
	}
}
#endif
/**
	met a jour le menu "Fenetres"
*/
void QETDiagramEditor::slot_updateWindowsMenu()
{
	// nettoyage du menu
	foreach(QAction *a, windows_menu -> actions()) windows_menu -> removeAction(a);

	// actions de fermeture
	windows_menu -> addAction(m_close_file);
	//windows_menu -> addAction(closeAllAct);

	// actions de reorganisation des fenetres
	windows_menu -> addSeparator();
	windows_menu -> addAction(m_tile_window);
	windows_menu -> addAction(m_cascade_window);

	// actions de deplacement entre les fenetres
	windows_menu -> addSeparator();
	windows_menu -> addAction(m_next_window);
	windows_menu -> addAction(m_previous_window);

	// liste des fenetres
	QList<ProjectView *> windows = openedProjects();

	m_tile_window    -> setEnabled(!windows.isEmpty() && m_workspace.viewMode() == QMdiArea::SubWindowView);
	m_cascade_window -> setEnabled(!windows.isEmpty() && m_workspace.viewMode() == QMdiArea::SubWindowView);
	m_next_window    -> setEnabled(windows.count() > 1);
	m_previous_window    -> setEnabled(windows.count() > 1);

	if (!windows.isEmpty()) windows_menu -> addSeparator();
	QActionGroup *windows_actions = new QActionGroup(this);
	foreach(ProjectView *project_view, windows) {
		QString pv_title = project_view -> windowTitle();
		QAction *action  = windows_menu -> addAction(pv_title);
		windows_actions -> addAction(action);
		action -> setStatusTip(QString(tr("Active le projet « %1 »")).arg(pv_title));
		action -> setCheckable(true);
		action -> setChecked(project_view == currentProjectView());
		connect(action, SIGNAL(triggered()), &windowMapper, SLOT(map()));
		windowMapper.setMapping(action, project_view);
	}
}

/**
	Edite les proprietes du schema diagram
	@param diagram_view schema dont il faut editer les proprietes
*/
void QETDiagramEditor::editDiagramProperties(DiagramView *diagram_view)
{
	if (ProjectView *project_view = findProject(diagram_view)) {
		activateProject(project_view);
		project_view -> editDiagramProperties(diagram_view);
	}
}

/**
	Edite les proprietes du schema diagram
	@param diagram schema dont il faut editer les proprietes
*/
void QETDiagramEditor::editDiagramProperties(Diagram *diagram)
{
	if (ProjectView *project_view = findProject(diagram)) {
		activateProject(project_view);
		project_view -> editDiagramProperties(diagram);
	}
}

/**
	Affiche les projets dans des fenetres.
*/
void QETDiagramEditor::setWindowedMode()
{
	m_workspace.setViewMode(QMdiArea::SubWindowView);
	m_windowed_view_mode -> setChecked(true);
	slot_updateWindowsMenu();
}

/**
	Affiche les projets dans des onglets.
*/
void QETDiagramEditor::setTabbedMode()
{
	m_workspace.setViewMode(QMdiArea::TabbedView);
	m_tabbed_view_mode -> setChecked(true);
	slot_updateWindowsMenu();
}

/**
	@brief QETDiagramEditor::readSettings
	Read the settings
*/
void QETDiagramEditor::readSettings()
{
	QSettings settings;

	// dimensions et position de la fenetre
	QVariant geometry = settings.value("diagrameditor/geometry");
	if (geometry.isValid()) restoreGeometry(geometry.toByteArray());

	// etat de la fenetre (barres d'outils, docks...)
	QVariant state = settings.value("diagrameditor/state");
	if (state.isValid()) restoreState(state.toByteArray());

	// gestion des projets (onglets ou fenetres)
	bool tabbed = settings.value("diagrameditor/viewmode", "tabbed") == "tabbed";
	if (tabbed) {
		setTabbedMode();
	} else {
		setWindowedMode();
	}
}

/**
	@brief QETDiagramEditor::writeSettings
	Write the settings
*/
void QETDiagramEditor::writeSettings()
{
	QSettings settings;
	settings.setValue("diagrameditor/geometry", saveGeometry());
	settings.setValue("diagrameditor/state", saveState());
}

/**
	Active le projet passe en parametre
	@param project Projet a activer
*/
void QETDiagramEditor::activateProject(QETProject *project)
{
	activateProject(findProject(project));
}

/**
	Active le projet passe en parametre
	@param project_view Projet a activer
*/
void QETDiagramEditor::activateProject(ProjectView *project_view)
{
	if (!project_view) return;
	activateWidget(project_view);
}

/**
	@brief QETDiagramEditor::projectWasClosed
	Manage the close of a project.
	@param project_view
*/
void QETDiagramEditor::projectWasClosed(ProjectView *project_view)
{
	QETProject *project = project_view -> project();
	if (project)
	{
		pa -> elementsPanel().projectWasClosed(project);
		m_element_collection_widget->removeProject(project);
		undo_group.removeStack(project -> undoStack());
		QETApp::unregisterProject(project);
	}
	//When project is closed, a lot of signals are emitted, notably if there is an item selected in a diagram.
	//In some special case, since signal/slot connection can be direct or queued, some signals are handled after QObject is deleted, and crash qet
	//notably in the function Diagram::elements when it calls items() (I don't know exactly why).
	//set nullptr to "m_selection_properties_editor->setDiagram()" fixes this crash
	m_selection_properties_editor->setDiagram(nullptr);
	project_view -> deleteLater();
	project -> deleteLater();
	updateWelcomeWidget();
}

/**
	Edite les proprietes du projet project_view.
	@param project_view Vue sur le projet dont il faut editer les proprietes
*/
void QETDiagramEditor::editProjectProperties(ProjectView *project_view)
{
	if (!project_view) return;
	activateProject(project_view);
	project_view -> editProjectProperties();
}

/**
	Edite les proprietes du projet project.
	@param project Projet dont il faut editer les proprietes
*/
void QETDiagramEditor::editProjectProperties(QETProject *project)
{
	editProjectProperties(findProject(project));
}

/**
	@brief QETDiagramEditor::addDiagramToProject
	Add a diagram to project
	@param project
*/
void QETDiagramEditor::addDiagramToProject(QETProject *project)
{
	if (!project) {
		return;
	}

	if (ProjectView *project_view = findProject(project))
	{
		activateProject(project);
		project_view->project()->addNewDiagram();
	}
}
/**
 * @brief QETDiagramEditor::removeDiagram
 * Wrapper für einzelne Diagramme, um Abwärtskompatibilität zu erhalten.
 */
void QETDiagramEditor::removeDiagram(Diagram *diagram)
{
	if (!diagram) return;
	QList<Diagram *> list;
	list << diagram;
	removeDiagrams(list);
}

/**
 * @brief QETDiagramEditor::removeDiagrams
 * Deletes a list of folios with a single query.
 */
void QETDiagramEditor::removeDiagrams(const QList<Diagram *> &diagrams)
{
	if (diagrams.isEmpty()) return;

	if (diagrams.count() == 1) {
		QMessageBox::StandardButton reply;
		reply = QMessageBox::question(this, tr("Supprimer le folio"),
									  tr("Êtes-vous sûr de vouloir supprimer ce folio ?"),
									  QMessageBox::Yes | QMessageBox::No);
		if (reply == QMessageBox::No) return;
	} else {
		QMessageBox::StandardButton reply;
		reply = QMessageBox::question(this, tr("Supprimer les folios"),
									  tr("Êtes-vous sûr de vouloir supprimer les %1 folios sélectionnés ?").arg(diagrams.count()),
									  QMessageBox::Yes | QMessageBox::No);
		if (reply == QMessageBox::No) return;
	}

	ProjectView *project_view = nullptr;
	if (QETProject *diagram_project = diagrams.first()->project()) {
		project_view = findProject(diagram_project);
	}

	if (project_view) project_view->setUpdatesEnabled(false);
	if (pa) pa->setUpdatesEnabled(false);

	foreach (Diagram *diagram, diagrams) {
		removeDiagramSilent(diagram);
	}

	if (pa) pa->setUpdatesEnabled(true);
	if (project_view) project_view->setUpdatesEnabled(true);

	emit syncElementsPanel();
}

/**
	Supprime un schema de son projet
	@param diagram Schema a supprimer
*/
void QETDiagramEditor::removeDiagramSilent(Diagram *diagram)
{
	if (!diagram) return;

	if (QETProject *diagram_project = diagram -> project()) {
		if (ProjectView *project_view = findProject(diagram_project)) {

			// supprime le schema
			project_view -> removeDiagram(diagram, true);
		}
	}
}
void QETDiagramEditor::moveDiagramUp(const QList<Diagram *> &diagrams) {
	if (diagrams.isEmpty()) return;
	QList<Diagram *> safeDiagrams = diagrams;
	if (QETProject *diagram_project = safeDiagrams.first()->project()) {
		if (!diagram_project->isReadOnly()) {
			if (ProjectView *project_view = findProject(diagram_project)) {
				// Forward loop for moving up
				for (int i = 0; i < safeDiagrams.size(); ++i) {
					project_view->moveDiagramUp(safeDiagrams.at(i));
					QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
				}
			}
		}
	}
}

void QETDiagramEditor::moveDiagramDown(const QList<Diagram *> &diagrams) {
	if (diagrams.isEmpty()) return;
	QList<Diagram *> safeDiagrams = diagrams;
	if (QETProject *diagram_project = safeDiagrams.first()->project()) {
		if (!diagram_project->isReadOnly()) {
			if (ProjectView *project_view = findProject(diagram_project)) {
				// Backward loop for moving down
				for (int i = safeDiagrams.size() - 1; i >= 0; --i) {
					project_view->moveDiagramDown(safeDiagrams.at(i));
					QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
				}
			}
		}
	}
}

void QETDiagramEditor::moveDiagramUpTop(const QList<Diagram *> &diagrams) {
	if (diagrams.isEmpty()) return;
	QList<Diagram *> safeDiagrams = diagrams;
	if (QETProject *diagram_project = safeDiagrams.first()->project()) {
		if (!diagram_project->isReadOnly()) {
			if (ProjectView *project_view = findProject(diagram_project)) {
				// Backward loop to preserve relative order of the selected items when moving to top
				for (int i = safeDiagrams.size() - 1; i >= 0; --i) {
					project_view->moveDiagramUpTop(safeDiagrams.at(i));
					QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
				}
			}
		}
	}
}

void QETDiagramEditor::moveDiagramUpx10(const QList<Diagram *> &diagrams) {
	if (diagrams.isEmpty()) return;
	QList<Diagram *> safeDiagrams = diagrams;
	if (QETProject *diagram_project = safeDiagrams.first()->project()) {
		if (!diagram_project->isReadOnly()) {
			if (ProjectView *project_view = findProject(diagram_project)) {
				// Forward loop for moving up
				for (int i = 0; i < safeDiagrams.size(); ++i) {
					project_view->moveDiagramUpx10(safeDiagrams.at(i));
					QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
				}
			}
		}
	}
}

void QETDiagramEditor::moveDiagramDownx10(const QList<Diagram *> &diagrams) {
	if (diagrams.isEmpty()) return;
	QList<Diagram *> safeDiagrams = diagrams;
	if (QETProject *diagram_project = safeDiagrams.first()->project()) {
		if (!diagram_project->isReadOnly()) {
			if (ProjectView *project_view = findProject(diagram_project)) {
				// Backward loop for moving down
				for (int i = safeDiagrams.size() - 1; i >= 0; --i) {
					project_view->moveDiagramDownx10(safeDiagrams.at(i));
					QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
				}
			}
		}
	}
}

void QETDiagramEditor::moveDiagramUpx100(const QList<Diagram *> &diagrams) {
	if (diagrams.isEmpty()) return;
	QList<Diagram *> safeDiagrams = diagrams;
	if (QETProject *diagram_project = safeDiagrams.first()->project()) {
		if (!diagram_project->isReadOnly()) {
			if (ProjectView *project_view = findProject(diagram_project)) {
				// Forward loop for moving up
				for (int i = 0; i < safeDiagrams.size(); ++i) {
					project_view->moveDiagramUpx100(safeDiagrams.at(i));
					QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
				}
			}
		}
	}
}

void QETDiagramEditor::moveDiagramDownx100(const QList<Diagram *> &diagrams) {
	if (diagrams.isEmpty()) return;
	QList<Diagram *> safeDiagrams = diagrams;
	if (QETProject *diagram_project = safeDiagrams.first()->project()) {
		if (!diagram_project->isReadOnly()) {
			if (ProjectView *project_view = findProject(diagram_project)) {
				// Backward loop for moving down
				for (int i = safeDiagrams.size() - 1; i >= 0; --i) {
					project_view->moveDiagramDownx100(safeDiagrams.at(i));
					QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
				}
			}
		}
	}
}

void QETDiagramEditor::reloadOldElementPanel()
{
	pa->reloadAndFilter();
}

/**
	Supprime le schema courant du projet courant
*/
void QETDiagramEditor::removeDiagramFromProject()
{
	if (ProjectView *current_project = currentProjectView()) {
		if (DiagramView *current_diagram = current_project -> currentDiagram()) {
			current_project -> removeDiagram(current_diagram);
		}
	}
}

/**
	@brief QETDiagramEditor::diagramWasAdded
	Manage the adding of diagram view in a project
	@param dv : added diagram view
*/
void QETDiagramEditor::diagramWasAdded(DiagramView *dv)
{
	connect(dv->diagram(),
		&QGraphicsScene::selectionChanged,
		this,
		&QETDiagramEditor::selectionChanged,
		Qt::DirectConnection);
	connect(dv,
		SIGNAL(modeChanged()),
		this,
		SLOT(slot_updateModeActions()));
}

/**
	@brief QETDiagramEditor::findElementInPanel
	Find the item for location in the element panel
	@param location
*/
void QETDiagramEditor::findElementInPanel(const ElementsLocation &location)
{
	m_element_collection_widget->setCurrentLocation(location);
}

/**
	Show the error message contained in \a result.
*/
void QETDiagramEditor::showError(const QETResult &result)
{
	if (result.isOk()) return;
	showError(result.errorMessage());
}

/**
	Show the \a error message.
*/
void QETDiagramEditor::showError(const QString &error)
{
	if (error.isEmpty()) return;
	QET::QetMessageBox::critical(this, tr("Erreur", "message box title"), error);
}

/**
	@brief QETDiagramEditor::subWindowActivated
	Slot used to update menu and undo stack when subwindows of MDIarea was activated
	@param subWindows
*/
void QETDiagramEditor::subWindowActivated(QMdiSubWindow *subWindows)
{
	Q_UNUSED(subWindows)

	slot_updateActions();
	slot_updateWindowsMenu();
	emit syncElementsPanel();
	updateWelcomeWidget();

	// 唯讀專案(檢視發行版/審核檢視)自動進瀏覽模式、隱藏編輯 UI。
	// 未命名的新專案(無檔案路徑)其 QFileInfo("").isWritable() 為 false 會被
	// 判成唯讀,須排除,否則新專案會被誤鎖成瀏覽模式。
	bool read_only = false;
	if (ProjectView *pv = currentProjectView())
		if (QETProject *proj = pv->project())
			read_only = proj->isReadOnly()
				    && !proj->filePath().isEmpty();
	applyReadOnlyView(read_only);
	updatePdmToolbar();
}

/**
	@brief QETDiagramEditor::applyReadOnlyView
	依目前開檔狀態切換編輯類 UI 的顯示:
	- 沒開任何圖時:隱藏所有編輯/檢視/繪圖工具鈕(只留新增/開啟/圖檔管理)。
	- 唯讀檢視(檢視發行版/審核檢視)時:強制瀏覽模式,隱藏會動到版面的編輯
	  工具(新增/刪除頁面、復原/重做、剪貼、刪除/旋轉、繪圖/深度/圖框工具列、
	  元件庫面板),但保留檢視工具列(模式鈕+縮放)供瀏覽。
	- 可編輯時:全部還原顯示。
	供 subWindowActivated 依專案唯讀狀態呼叫。
*/
void QETDiagramEditor::applyReadOnlyView(bool read_only)
{
	const bool has_project = currentProjectView() != nullptr;
	const bool editable = has_project && !read_only;

	// 唯讀才強制把 folio 切為瀏覽模式;可編輯專案不強制改模式(避免干擾
	// 開啟流程),僅同步工具列模式鈕的勾選/啟用狀態。
	if (read_only) {
		if (ProjectView *pv = currentProjectView())
			for (DiagramView *dv : pv->diagram_views())
				dv->setVisualisationMode();
	}
	if (m_mode_selection) {
		m_mode_selection->setChecked(!read_only);
		m_mode_selection->setEnabled(editable);
	}
	if (m_mode_visualise) m_mode_visualise->setChecked(read_only);

	// 面板與繪圖類工具列:僅可編輯時顯示
	if (qdw_pa) qdw_pa->setVisible(editable);
	if (m_add_item_tool_bar) m_add_item_tool_bar->setVisible(editable);
	if (m_depth_tool_bar) m_depth_tool_bar->setVisible(editable);
	if (diagram_tool_bar) diagram_tool_bar->setVisible(editable);

	// 編輯工具列(新增/刪除頁面、復原/重做、剪貼、刪除/旋轉):僅可編輯時顯示
	if (edit_tool_bar) edit_tool_bar->setVisible(editable);

	// 檢視工具列(模式鈕+縮放):有開圖就顯示(瀏覽時仍可縮放),無開圖隱藏
	if (view_tool_bar) view_tool_bar->setVisible(has_project);
}

/**
	@brief QETDiagramEditor::selectionChanged
	This slot is called when a diagram selection was changed.
*/
void QETDiagramEditor::selectionChanged()
{
	slot_updateComplexActions();

	DiagramView *dv = currentDiagramView();
	if (dv && dv->diagram())
		m_selection_properties_editor->setDiagram(dv->diagram());
}


/**
	@brief QETDiagramEditor::generateTerminalBlock
*/
void QETDiagramEditor::generateTerminalBlock()
{
#ifdef TODO_LIST
#	pragma message("@TODO Merge 'qet_tb_generator' code in to Qet")
#	pragma message("https://github.com/qelectrotech/qet_tb_generator")
#endif

	bool success = false;
	QList<QString> exeList;
	QProcess *process = new QProcess(qApp);

#if defined(Q_OS_WIN32) || defined(Q_OS_WIN64)
	exeList << (QETApp::dataDir() + "/binary/qet_tb_generator.exe")
			<< (QDir::currentPath() + "/qet_tb_generator.exe")
			<< QStandardPaths::findExecutable("qet_tb_generator.exe")
			<< (QDir::homePath() + "/Application Data/qet/qet_tb_generator.exe")
			<< "qet_tb_generator.exe"
			<< "qet_tb_generator";    // from original code: missing ".exe" ???
#elif  defined(Q_OS_MACOS)
	exeList << (QETApp::dataDir() + "/binary/qet_tb_generator")
			<< (QDir::currentPath() + "/qet_tb_generator")
			<< QStandardPaths::findExecutable("qet_tb_generator")
			<< (QDir::homePath() + "/.qet/qet_tb_generator.app")
			<< "/Library/Frameworks/Python.framework/Versions/3.11/bin/qet_tb_generator";
#else
	exeList << (QETApp::dataDir() + "/binary/qet_tb_generator")
			<< (QDir::currentPath() + "/qet_tb_generator")
			<< (QDir::homePath() + "/.qet/qet_tb_generator")
			<< QStandardPaths::findExecutable("qet_tb_generator")
			<< "qet_tb_generator";
#endif

		// If launched under control:
		//connect(process, SIGNAL(errorOcurred(int error)), this, SLOT(slot_generateTerminalBlock_error()));
		//process->start("qet_tb_generator");

	qInfo() << " project to use for qet_tb_generator: "
			<< (QETDiagramEditor::currentProjectView()->project()->filePath());

	if (openedProjects().count()) {
		foreach(QString exe, exeList) {
			if ((success == false) && exe.length() && QFile::exists(exe)) {
				success = process->startDetached(exe, {(QETDiagramEditor::currentProjectView()->project()->filePath())});
			}
			if (success == true) {
				qInfo() << " qet_tb_generator found here:" << exe;
				break;
			} else {
				qInfo() << " qet_tb_generator not found :" << exe;
			}
		}
	} else {
		qInfo() << "No project loaded - no need to start \"qet_tb_generator\"";
	}
	process->close();

#if defined(Q_OS_WIN32) || defined(Q_OS_WIN64)
	QString message=QObject::tr(
		"To install the plugin qet_tb_generator"
		"<br>Visit :"
		"<br>"
		"<a href='https://pypi.python.org/pypi/qet-tb-generator'>qet-tb-generator</a>"
		"<br>Requires python 3.5 or above."
		"<br><B><U> First install on Windows</B></U>"
		"<br>1. Install, if required, python 3.5 or above"
		"<br> Visit :"
		"<br>"
		"<a href='https://www.python.org/downloads/'>python.org</a>"
		"<br>2. pip install qet_tb_generator"
		"<br><B><U> Update on Windows</B></U>"
		"<br>python -m pip install --upgrade qet_tb_generator"
		"<br>"
		">>user could launch in a terminal this script in this directory"
		"<br>"
		" C:\\users\\XXXX\\AppData\\Local\\Programs\\Python\\Python36-32\\Scripts   "
		"<br>");
#elif defined(Q_OS_MACOS)
	QString message=QObject::tr(
		"To install the plugin qet_tb_generator"
		"<br>Visit  :"
		"<br>"
		"<a href='https://pypi.python.org/pypi/qet-tb-generator'>qet-tb-generator</a>"
		"<br><B><U> First install on macOSX</B></U>"
		"<br>1. Install, if required, python 3.11 bundle only, "
		"<a href='https://www.python.org/ftp/python/3.11.2/python-3.11.2-macos11.pkg'>python-3.11.2-macos11.pkg</a>"
		"<br>2 Run Profile.command script"
		"<br>"
		"because program use hardcoded PATH for localise qet-tb-generator plugin "
		"<br> Visit :"
		"<br>"
		"<a href='https://qelectrotech.org/forum/viewtopic.php?pid=5674#p5674'>howto</a>"
		"<br>2. pip3 install qet_tb_generator"
		"<br><B><U> Update on macOSX</B></U>"
		"<br> pip3 install --upgrade qet_tb_generator"
		"<br>");
#else
	QString message=QObject::tr(
		"To install the plugin qet_tb_generator"
		"<br>Visit :"
		"<br>"
		"<a href='https://pypi.python.org/pypi/qet-tb-generator'>qet-tb-generator</a>"
		"<br>"
		"<br>Requires python 3.5 or above."
		"<br>"
		"<br><B><U> First install on Linux</B>""</U>"
		"<br>1. check you have pip3 installed: pip3 --version"
		"<br>If not install with: sudo apt-get install python3-pip"
		"<br>2. Install the program: sudo pip3 install qet_tb_generator"
		"<br>3. Run the program: qet_tb_generator"
		"<br>"
		"<br><B>""<U> Update on Linux</B>""</U>"
		"<br>sudo pip3 install --upgrade qet_tb_generator"
		"<br>");
#endif
	if ( !success ) {
		QMessageBox::warning(nullptr,
							 QObject::tr("Error launching qet_tb_generator plugin"),
							 message);
	}
}

/**
 * @brief QETDiagramEditor::slot_terminalNumbering
 * Opens the dialog for automatic terminal numbering and applies the generated undo command.
 */
void QETDiagramEditor::slot_terminalNumbering() {
	TerminalNumberingDialog dialog(this);
	if (dialog.exec() == QDialog::Accepted) {
		QETProject *project = currentProject();
		if (!project) return;

		// Fetch the generated undo command from the dialog logic
		QUndoCommand *macro = dialog.getUndoCommand(project);

		// If changes were made, push them to the global undo stack
		if (macro) {
			undo_group.activeStack()->push(macro);
		}
	}
}
