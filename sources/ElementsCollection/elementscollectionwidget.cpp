/*
	Copyright 2006-2026 The QElectroTech Team
	This file is part of QElectroTech.

	QElectroTech is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	QElectroTech is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with QElectroTech. If not, see <http://www.gnu.org/licenses/>.
*/
#include "elementscollectionwidget.h"

#include "../editor/ui/qetelementeditor.h"
#include "../elementscategoryeditor.h"
#include "../newelementwizard.h"
#include "../diagram.h"
#include "../diagramevent/diagrameventaddelement.h"
#include "../diagramview.h"
#include "../projectview.h"
#include "../qetapp.h"
#include "../qetdiagrameditor.h"
#include "../qeticons.h"
#include "../qetmessagebox.h"
#include "../qetproject.h"
#include "elementcollectionitem.h"
#include "elementscollectionmodel.h"
#include "elementslocation.h"
#include "elementstreeview.h"
#include "../factory/elementpicturefactory.h"
#include "fileelementcollectionitem.h"
#include "xmlprojectelementcollectionitem.h"

#include <QDesktopServices>
#include <QMenu>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QtGlobal>
#include <QProgressBar>
#include <QStatusBar>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPicture>
#include <QStyledItemDelegate>
#include <functional>
#include <QSettings>
#include <QSplitter>

/**
	Delegate of the grid view : renders the element picture vectorially
	at the configured icon size (crisp at any size, hidpi aware) instead
	of upscaling the small cached pixmap, and draws the FluidSIM-like
	framed cell with the label below.
*/
class GridElementDelegate : public QStyledItemDelegate
{
	public:
	GridElementDelegate(
		std::function<QString(const QModelIndex &)> path_for_index,
		QObject *parent) :
		QStyledItemDelegate(parent),
		m_path_for_index(std::move(path_for_index))
	{}

	void setIconSize(int size)
	{
		if (m_icon_size != size) {
			m_icon_size = size;
			m_pixmap_cache.clear();
		}
	}

	void paint(QPainter *painter,
		   const QStyleOptionViewItem &option,
		   const QModelIndex &index) const override
	{
		painter->save();
		const QRect cell = option.rect.adjusted(2, 2, -2, -2);
		painter->setClipRect(cell);

		/* le cadre n'entoure que l'icone ; le libelle est dessine
		 * sous le cadre pour ne jamais chevaucher le dessin */
		const bool selected = option.state & QStyle::State_Selected;
		const QRect box(cell.left(), cell.top(), cell.width(),
				m_icon_size + 6);
		painter->fillRect(box, selected
			? option.palette.highlight()
			: option.palette.base());
		painter->setPen(QColor(0xc8, 0xc8, 0xc8));
		painter->drawRect(box.adjusted(0, 0, -1, -1));

		const QRect icon_rect(box.left(), box.top() + 3,
				      box.width(), m_icon_size);
		const QString path = m_path_for_index(index);
		QPixmap pixmap;
		if (!path.isEmpty()
		    && path.endsWith(QLatin1String(".elmt"))) {
			pixmap = elementPixmap(
				path, option.widget
					? option.widget->devicePixelRatioF()
					: 1.0);
		}
		if (!pixmap.isNull()) {
			const QSizeF logical =
				pixmap.deviceIndependentSize();
			painter->drawPixmap(
				icon_rect.center()
					- QPoint(logical.width() / 2,
						 logical.height() / 2),
				pixmap);
		} else {
			index.data(Qt::DecorationRole).value<QIcon>().paint(
				painter, icon_rect, Qt::AlignCenter);
		}

		const QRect text_rect(cell.left() + 2, box.bottom() + 2,
				      cell.width() - 4,
				      cell.bottom() - box.bottom() - 4);
		painter->setPen(option.palette.text().color());
		painter->setFont(option.font);
		painter->drawText(text_rect,
				  Qt::AlignHCenter | Qt::AlignTop
					  | Qt::TextWordWrap,
				  index.data().toString());
		painter->restore();
	}

	private:
	QPixmap elementPixmap(const QString &path, qreal dpr) const
	{
		const QString key = path + QChar('@')
				    + QString::number(m_icon_size)
				    + QChar('x') + QString::number(dpr);
		const auto it = m_pixmap_cache.constFind(key);
		if (it != m_pixmap_cache.constEnd()) {
			return *it;
		}

		ElementsLocation location(path);
		QPicture picture, low_picture;
		ElementPictureFactory::instance()->getPictures(
			location, picture, low_picture);
		const QRect bounding = picture.boundingRect();
		QPixmap pixmap;
		if (!bounding.isEmpty()) {
			const qreal margin = 4;
			const qreal scale = qMin(
				(m_icon_size - margin) / qreal(bounding.width()),
				(m_icon_size - margin) / qreal(bounding.height()));
			pixmap = QPixmap(QSize(m_icon_size, m_icon_size) * dpr);
			pixmap.setDevicePixelRatio(dpr);
			pixmap.fill(Qt::transparent);
			QPainter p(&pixmap);
			p.setRenderHint(QPainter::Antialiasing);
			p.translate(QPointF(m_icon_size / 2.0, m_icon_size / 2.0)
				    - QPointF(bounding.center()) * scale);
			p.scale(scale, scale);
			p.drawPicture(0, 0, picture);
		}
		m_pixmap_cache.insert(key, pixmap);
		return pixmap;
	}

	std::function<QString(const QModelIndex &)> m_path_for_index;
	int m_icon_size = 60;
	mutable QHash<QString, QPixmap> m_pixmap_cache;
};

/**
	@brief ElementsCollectionWidget::ElementsCollectionWidget
	Default constructor.
	@param parent : parent widget of this widget.
*/
ElementsCollectionWidget::ElementsCollectionWidget(QWidget *parent):
	QWidget(parent),
	m_model(nullptr)
{
	//The connection in the method ElementsCollectionWidget::reload
	//return a warning message at compilation :
	//**********
	//QObject::connect: Cannot queue arguments of type 'QVector<int>'
	//(Make sure 'QVector<int>' is registered using qRegisterMetaType().)
	//**********
	//Register meta type has recommended by the message.
	qRegisterMetaType<QVector<int>>();

	setUpWidget();
	setUpAction();
	setUpConnection();

	//Timer is used to avoid launching a new search for each letter typed by user
	//Timer is started or restarted at every time user type a new letter.
	//When the timer emit timeout, we start the search.
	m_search_timer.setInterval(500);
	m_search_timer.setSingleShot(true);
}

/**
	@brief ElementsCollectionWidget::expandFirstItems
	Expand each first item in the tree view
*/
void ElementsCollectionWidget::expandFirstItems()
{
	if (!m_model)
		return;

	for (int i=0; i < m_model->rowCount() ; i++)
		showAndExpandItem(m_model->index(i, 0), false);
}

/**
	@brief ElementsCollectionWidget::addProject
	Add project to be displayed
	@param project
*/
void ElementsCollectionWidget::addProject(QETProject *project)
{
	if (m_model)
	{
		m_model->addProject(project, true);
	}
	else {
		m_waiting_project.append(project);
	}
}

void ElementsCollectionWidget::removeProject(QETProject *project) {
	if (m_model)
		m_model->removeProject(project);
}

/**
	@brief ElementsCollectionWidget::highlightUnusedElement
	highlight the unused element
	@see ElementsCollectionModel::highlightUnusedElement()
*/
void ElementsCollectionWidget::highlightUnusedElement()
{
	if (m_model)
		m_model->highlightUnusedElement();
}

/**
	@brief ElementsCollectionWidget::setCurrentLocation
	Set the current item to be the item for location
	@param location
*/
void ElementsCollectionWidget::setCurrentLocation(
		const ElementsLocation &location)
{
	if (!location.exist())
		return;

	if (m_model)
		m_tree_view->setCurrentIndex(
					m_model->indexFromLocation(location));
}

void ElementsCollectionWidget::leaveEvent(QEvent *event)
{
	if (QETDiagramEditor *qde = QETApp::diagramEditorAncestorOf(this))
		qde->statusBar()->clearMessage();

	QWidget::leaveEvent(event);
}

/**
	Icon size and items-per-row of the grid view, from the general
	configuration (elementspanel/grid-icon-size, elementspanel/grid-columns;
	columns = 0 means as many as the width allows).
*/
void ElementsCollectionWidget::applyGridDisplaySettings()
{
	QSettings settings;
	const int icon_size = qBound(
		24,
		settings.value(QStringLiteral("elementspanel/grid-icon-size"),
			       60).toInt(),
		256);
	m_grid_columns = qBound(
		0,
		settings.value(QStringLiteral("elementspanel/grid-columns"),
			       0).toInt(),
		30);
	m_grid_cell_width = qBound(
		0,
		settings.value(QStringLiteral("elementspanel/grid-cell-width"),
			       0).toInt(),
		600);
	m_grid_view->setIconSize(QSize(icon_size, icon_size));
	m_grid_delegate->setIconSize(icon_size);
	updateGridGeometry();
}

void ElementsCollectionWidget::updateGridGeometry()
{
	const int icon = m_grid_view->iconSize().width();
	const int text_h = m_grid_view->fontMetrics().height() * 2;
	int cell_w = icon + 28;
	if (m_grid_cell_width > 0) {
		cell_w = m_grid_cell_width;
	} else if (m_grid_columns > 0) {
		cell_w = qMax(icon + 8,
			      m_grid_view->viewport()->width()
				      / m_grid_columns - 1);
	}
	m_grid_view->setGridSize(QSize(cell_w, icon + text_h + 20));
}

/**
	@brief ElementsCollectionWidget::updateGridRoot
	Show in the grid view the content of the directory clicked in the
	tree (or the directory of the clicked element).
*/
void ElementsCollectionWidget::updateGridRoot(const QModelIndex &index)
{
	if (!m_grid_view->model()) return;
	ElementCollectionItem *eci = elementCollectionItemForIndex(index);
	if (!eci) return;

	QModelIndex root = index;
	if (eci->isElement()) {
		root = index.parent();
	}

	/* les donnees des items (dont le libelle) sont remplies en tache de
	 * fond ; les dossiers jamais deployes dans l'arbre peuvent encore
	 * avoir des enfants sans texte : les completer avant affichage */
	if (ElementCollectionItem *dir_item =
			elementCollectionItemForIndex(root)) {
		for (int i = 0 ; i < dir_item->rowCount() ; ++i) {
			auto *child = static_cast<ElementCollectionItem *>(
				dir_item->child(i));
			if (child && child->text().isEmpty()) {
				child->setUpData();
			}
		}
	}

	m_grid_view->setRootIndex(root);
	if (eci->isElement()) {
		m_grid_view->setCurrentIndex(index);
	}
}

/**
	Space key on an element of the tree starts the click-to-place mode on
	the current diagram (same interface as drag and drop: left click place
	the element, space rotate it, right click / escape finish).
*/
bool ElementsCollectionWidget::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == m_grid_view && event->type() == QEvent::Resize
	    && m_grid_columns > 0) {
		updateGridGeometry();
	}
	if ((watched == m_tree_view || watched == m_grid_view)
	    && event->type() == QEvent::KeyPress
	    && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Space) {
		placeElementAtIndex(
			static_cast<QAbstractItemView *>(watched)->currentIndex());
		return true;
	}
	return QWidget::eventFilter(watched, event);
}

void ElementsCollectionWidget::placeElementAtIndex(const QModelIndex &index)
{
	ElementCollectionItem *eci = elementCollectionItemForIndex(index);
	if (!(eci && eci->isElement())
	    || eci->collectionPath().endsWith(QLatin1String(".qetmak"))) {
		return;
	}

	QETDiagramEditor *qde = QETApp::diagramEditorAncestorOf(this);
	if (!qde) return;
	ProjectView *pv = qde->currentProjectView();
	if (!pv) return;
	DiagramView *dv = pv->currentDiagram();
	if (!(dv && dv->diagram())) return;

	ElementsLocation location(eci->collectionPath());
	dv->diagram()->setEventInterface(new DiagramEventAddElement(
		location, dv->diagram(),
		dv->mapToScene(dv->viewport()->rect().center())));
	dv->setFocus();
}

void ElementsCollectionWidget::setUpAction()
{
	m_open_dir = new QAction(QET::Icons::FolderOpen,
				 tr("Ouvrir le dossier correspondant"), this);
	m_edit_element = new QAction(QET::Icons::ElementEdit,
					 tr("Éditer l'élément"), this);
	m_delete_element = new QAction(QET::Icons::ElementDelete,
					   tr("Supprimer l'élément"), this);
	m_delete_dir = new QAction(QET::Icons::FolderDelete,
				   tr("Supprimer le dossier"), this);
	m_reload = new QAction(QET::Icons::ViewRefresh,
				   tr("Recharger les collections"), this);
	m_edit_dir = new QAction(QET::Icons::FolderEdit,
				 tr("Éditer le dossier"), this);
	m_new_directory = new QAction(QET::Icons::FolderNew,
					  tr("Nouveau dossier"), this);
	m_new_element = new QAction(QET::Icons::ElementNew,
					tr("Nouvel élément"), this);
	m_show_this_dir = new QAction(QET::Icons::FolderOnlyThis,
					  tr("Afficher uniquement ce dossier"),
					  this);
	m_show_all_dir = new QAction(QET::Icons::FolderShowAll,
					 tr("Afficher tous les dossiers"), this);
	m_dir_propertie = new QAction(QET::Icons::FolderProperties,
					  tr("Propriété du dossier"), this);
}

/**
	@brief ElementsCollectionWidget::setUpWidget
	Setup this widget
*/
void ElementsCollectionWidget::setUpWidget()
{
	m_main_vlayout = new QVBoxLayout(this);
	m_main_vlayout->setContentsMargins(0, 0, 0, 0);
	m_main_vlayout->setSpacing(2);

	m_search_field = new QLineEdit(this);
	m_search_field->setPlaceholderText(tr("Rechercher..."));
	m_search_field->setClearButtonEnabled(true);

	m_tree_view = new ElementsTreeView(this);
	m_tree_view->setHeaderHidden(true);
	m_tree_view->setIconSize(QSize(50, 50));
	m_tree_view->setDragDropMode(QAbstractItemView::DragDrop);
	m_tree_view->setContextMenuPolicy(Qt::CustomContextMenu);
	m_tree_view->setAutoExpandDelay(500);
	m_tree_view->setAnimated(true);
	m_tree_view->setMouseTracking(true);
	m_tree_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	m_tree_view->installEventFilter(this);

	/* vue en grille du dossier selectionne : icones plus grandes,
	 * texte reduit, beaucoup plus d'elements visibles a la fois */
	m_grid_view = new QListView(this);
	m_grid_view->setViewMode(QListView::IconMode);
	m_grid_view->setResizeMode(QListView::Adjust);
	m_grid_view->setMovement(QListView::Static);
	m_grid_view->setWrapping(true);
	m_grid_view->setUniformItemSizes(true);
	m_grid_view->setWordWrap(true);
	m_grid_view->setTextElideMode(Qt::ElideRight);
	m_grid_view->setDragEnabled(true);
	m_grid_view->setDragDropMode(QAbstractItemView::DragOnly);
	m_grid_view->setSelectionMode(QAbstractItemView::SingleSelection);
	m_grid_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	QFont grid_font = m_grid_view->font();
	grid_font.setPointSizeF(grid_font.pointSizeF() * 0.8);
	m_grid_view->setFont(grid_font);
	m_grid_delegate = new GridElementDelegate(
		[this](const QModelIndex &index) -> QString {
			ElementCollectionItem *eci =
				elementCollectionItemForIndex(index);
			return (eci && eci->isElement())
				       ? eci->collectionPath()
				       : QString();
		},
		m_grid_view);
	m_grid_view->setItemDelegate(m_grid_delegate);
	applyGridDisplaySettings();
	connect(QETApp::instance(), &QETApp::settingsChanged,
		this, &ElementsCollectionWidget::applyGridDisplaySettings);
	m_grid_view->installEventFilter(this);

	//Setup the macros tree view
	m_macros_tree_view = new ElementsTreeView(this);
	m_macros_tree_view->setHeaderHidden(true);
	m_macros_tree_view->setIconSize(QSize(50, 50));
	m_macros_tree_view->setDragDropMode(QAbstractItemView::DragDrop);
	m_macros_tree_view->setContextMenuPolicy(Qt::CustomContextMenu);
	m_macros_tree_view->setAutoExpandDelay(500);
	m_macros_tree_view->setAnimated(true);
	m_macros_tree_view->setMouseTracking(true);
	m_macros_tree_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

	m_tab_widget = new QTabWidget(this);
	m_tab_widget->setDocumentMode(true);
	m_tab_widget->setTabPosition(QTabWidget::North);
	auto *collections_splitter = new QSplitter(Qt::Vertical, this);
	collections_splitter->addWidget(m_tree_view);
	collections_splitter->addWidget(m_grid_view);
	collections_splitter->setSizes({500, 350});
	m_tab_widget->addTab(collections_splitter, tr("Collections"));
	m_tab_widget->addTab(m_macros_tree_view, tr("Modèles"));

	m_main_vlayout->addWidget(m_search_field);
	m_main_vlayout->addWidget(m_tab_widget);

	m_progress_bar = new QProgressBar(this);
	m_progress_bar->setFormat(QObject::tr("chargement %p% (%v sur %m)"));
	m_main_vlayout->addWidget(m_progress_bar);
	m_progress_bar->hide();

	m_context_menu = new QMenu(this);
}

/**
	@brief ElementsCollectionWidget::setUpConnection
	Setup the connection used in this widget
*/
void ElementsCollectionWidget::setUpConnection()
{
	connect(m_tree_view, &QTreeView::customContextMenuRequested,
		this, &ElementsCollectionWidget::customContextMenu);
	connect(m_tree_view, &QTreeView::clicked,
		this, &ElementsCollectionWidget::updateGridRoot);
	connect(m_grid_view, &QListView::doubleClicked,
		this, [this](const QModelIndex &index) {
		ElementCollectionItem *eci = elementCollectionItemForIndex(index);
		if (!eci) return;
		if (eci->isElement()) {
			placeElementAtIndex(index);
		} else {
			m_tree_view->setCurrentIndex(index);
			m_tree_view->expand(index);
			updateGridRoot(index);
		}
	});
	connect(m_search_field, &QLineEdit::textEdited,
		[this]() {m_search_timer.start();});
	connect(&m_search_timer, &QTimer::timeout,
		this, &ElementsCollectionWidget::search);
	connect(m_open_dir, &QAction::triggered,
		this, &ElementsCollectionWidget::openDir);
	connect(m_edit_element, &QAction::triggered,
		this, &ElementsCollectionWidget::editElement);
	connect(m_delete_element, &QAction::triggered,
		this, &ElementsCollectionWidget::deleteElement);
	connect(m_delete_dir, &QAction::triggered,
		this, &ElementsCollectionWidget::deleteDirectory);
	connect(m_reload, &QAction::triggered,
		this, &ElementsCollectionWidget::reload);
	connect(m_edit_dir, &QAction::triggered,
		this, &ElementsCollectionWidget::editDirectory);
	connect(m_new_directory, &QAction::triggered,
		this, &ElementsCollectionWidget::newDirectory);
	connect(m_new_element, &QAction::triggered,
		this, &ElementsCollectionWidget::newElement);
	connect(m_show_this_dir, &QAction::triggered,
		this, &ElementsCollectionWidget::showThisDir);
	connect(m_show_all_dir, &QAction::triggered,
		this, &ElementsCollectionWidget::resetShowThisDir);
	connect(m_dir_propertie, &QAction::triggered,
		this, &ElementsCollectionWidget::dirProperties);

	connect(m_tree_view, &QTreeView::doubleClicked,
			[this](const QModelIndex &index)
			{
				this->m_index_at_context_menu = index ;
				ElementCollectionItem *eci = elementCollectionItemForIndex(index);
				if (eci && eci->collectionPath().endsWith(".qetmak")) {
					return; // Do nothing on double click for macros
				}
				this->editElement();
			});

	connect(m_tree_view, &QTreeView::entered,
		[this] (const QModelIndex &index) {
		QETDiagramEditor *qde = QETApp::diagramEditorAncestorOf(this);
		ElementCollectionItem *eci = elementCollectionItemForIndex(index);
		if (qde && eci)
			qde->statusBar()->showMessage(eci->localName());
	});

	connect(m_macros_tree_view, &QTreeView::customContextMenuRequested,
			this, &ElementsCollectionWidget::customContextMenu);

	connect(m_macros_tree_view, &QTreeView::doubleClicked,
			[this](const QModelIndex &index)
			{
				this->m_index_at_context_menu = index ;
				ElementCollectionItem *eci = elementCollectionItemForIndex(index);
				if (eci && eci->collectionPath().endsWith(".qetmak")) {
					return; // Do nothing on double click for macros
				}
				this->editElement();
			});

	connect(m_macros_tree_view, &QTreeView::entered,
		[this] (const QModelIndex &index) {
			QETDiagramEditor *qde = QETApp::diagramEditorAncestorOf(this);
			ElementCollectionItem *eci = elementCollectionItemForIndex(index);
			if (qde && eci)
				qde->statusBar()->showMessage(eci->localName());
		});
}

/**
 * @brief ElementsCollectionWidget::customContextMenu
 * Display the context menu of this widget at point
 * @param point
 */
void ElementsCollectionWidget::customContextMenu(const QPoint &point)
{
	QTreeView *clicked_tree = qobject_cast<QTreeView *>(sender());
	if (!clicked_tree) clicked_tree = m_tree_view; // Fallback

	m_index_at_context_menu = clicked_tree->indexAt(point);
	if (!m_index_at_context_menu.isValid()) return;

	m_context_menu->clear();

	ElementCollectionItem *eci = elementCollectionItemForIndex(
		m_index_at_context_menu);
	bool add_open_dir = false;

	if (eci->isElement() && !eci->collectionPath().endsWith(".qetmak"))
		m_context_menu->addAction(m_edit_element);

	if (eci->type() == FileElementCollectionItem::Type)
	{
		add_open_dir = true;
		FileElementCollectionItem *feci =
		static_cast<FileElementCollectionItem*>(eci);
		if (!feci->isCommonCollection())
		{
			if (feci->isDir())
			{
				if (!feci->isMacrosCollection()) {
					m_context_menu->addAction(m_new_element);
				}
				m_context_menu->addAction(m_new_directory);
				if (!feci->isCollectionRoot())
				{
					m_context_menu->addAction(m_edit_dir);
					m_context_menu->addAction(m_delete_dir);
				}
			}
			else
				m_context_menu->addAction(m_delete_element);
		}
	}
	if (eci->type() == XmlProjectElementCollectionItem::Type)
	{
		XmlProjectElementCollectionItem *xpeci =
		static_cast<XmlProjectElementCollectionItem *>(eci);
		if (xpeci->isCollectionRoot())
			add_open_dir = true;
	}

	m_context_menu->addSeparator();
	if (eci->isDir())
	{
		m_context_menu->addAction(m_show_this_dir);
		//there is a current filtered dir, add entry to reset it
		if (m_showed_index.isValid())
			m_context_menu->addAction(m_show_all_dir);

		m_context_menu->addAction(m_dir_propertie);
	}
	if (add_open_dir)
		m_context_menu->addAction(m_open_dir);
	m_context_menu->addAction(m_reload);

	m_context_menu->popup(mapToGlobal(clicked_tree->mapToParent(point)));
}

/**
	@brief ElementsCollectionWidget::openDir
	Open the directory represented by the current selected item
*/
void ElementsCollectionWidget::openDir()
{
	ElementCollectionItem *eci =
			elementCollectionItemForIndex(m_index_at_context_menu);
	if (!eci) return;

	if (eci->type() == FileElementCollectionItem::Type)

#ifdef Q_OS_LINUX
		QDesktopServices::openUrl(static_cast<FileElementCollectionItem*>(eci)->dirPath());
#else
		QDesktopServices::openUrl(QUrl("file:///" + static_cast<FileElementCollectionItem*>(eci)->dirPath()));
#endif
	else if (eci->type() == XmlProjectElementCollectionItem::Type)

#ifdef Q_OS_LINUX
		QDesktopServices::openUrl(static_cast<XmlProjectElementCollectionItem*>(eci)->project()->currentDir());
#else
		QDesktopServices::openUrl(QUrl("file:///" + static_cast<XmlProjectElementCollectionItem*>(eci)->project()->currentDir()));
#endif

}

/**
	@brief ElementsCollectionWidget::editElement
	Edit the element represented by the current selected item
*/
void ElementsCollectionWidget::editElement()
{
	ElementCollectionItem *eci = elementCollectionItemForIndex(m_index_at_context_menu);

	if ( !(eci && eci->isElement()) ) return;

	// Prevent the element editor from opening for macros
	if (eci->collectionPath().endsWith(".qetmak")) return;

	ElementsLocation location(eci->collectionPath());

	QETApp *app = QETApp::instance();
	app->openElementLocations(QList<ElementsLocation>() << location);

	foreach (QETElementEditor *element_editor, app->elementEditors())
		connect(element_editor,
			&QETElementEditor::saveToLocation,
			this,
			&ElementsCollectionWidget::locationWasSaved);
}

/**
	@brief ElementsCollectionWidget::deleteElement
	Delete the element represented by the current selected item.
*/
void ElementsCollectionWidget::deleteElement()
{
	ElementCollectionItem *eci = elementCollectionItemForIndex(
				m_index_at_context_menu);

	if (!eci) return;

	ElementsLocation loc(eci->collectionPath());

	bool isDeletableFile = loc.isElement() || eci->collectionPath().endsWith(".qetmak");

	if (! (isDeletableFile
		&& loc.exist()
		&& loc.isFileSystem()
		&& (loc.collectionPath().startsWith("company://")
		|| loc.collectionPath().startsWith("custom://")
		|| loc.collectionPath().startsWith("macros://"))) ) return;

	if (QET::QetMessageBox::question(
		this,
		tr("Supprimer l'élément ?", "message box title"),
		tr("Êtes-vous sûr  de vouloir supprimer cet élément ?\n",
		   "message box content"),
		QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
	{
		QFile file(loc.fileSystemPath());
		if (file.remove())
		{
			QAbstractItemModel *clicked_model = const_cast<QAbstractItemModel*>(m_index_at_context_menu.model());
			if (clicked_model) {
				clicked_model->removeRows(m_index_at_context_menu.row(), 1, m_index_at_context_menu.parent());
			}
		}
		else
		{
			QET::QetMessageBox::warning(
				this,
				tr("Suppression de l'élément",
				   "message box title"),
				tr("La suppression de l'élément a échoué.",
				   "message box content"));
		}
	}
}

/**
	@brief ElementsCollectionWidget::deleteDirectory
	Delete directory represented by the current selected item
*/
void ElementsCollectionWidget::deleteDirectory()
{
	ElementCollectionItem *eci = elementCollectionItemForIndex(
				m_index_at_context_menu);

	if (!eci) return;

	ElementsLocation loc (eci->collectionPath());
	if (! (loc.isDirectory()
		&& loc.exist()
		&& loc.isFileSystem()
		&& (loc.collectionPath().startsWith("company://")
		|| loc.collectionPath().startsWith("custom://")
		|| loc.collectionPath().startsWith("macros://"))) ) return;

	if (QET::QetMessageBox::question(
		this,
		tr("Supprimer le dossier?", "message box title"),
		tr("Êtes-vous sûr  de vouloir supprimer le dossier ?\n"
		"Tout les éléments et les dossier contenus dans ce dossier seront supprimés.",
		"message box content"),
		QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
	{
		QDir dir (loc.fileSystemPath());
		if (dir.removeRecursively())
		{
			QAbstractItemModel *clicked_model = const_cast<QAbstractItemModel*>(m_index_at_context_menu.model());
			if (clicked_model) {
				clicked_model->removeRows(m_index_at_context_menu.row(), 1, m_index_at_context_menu.parent());
			}
		}
		else
		{
			QET::QetMessageBox::warning(
				this,
				tr("Suppression du dossier",
				   "message box title"),
				tr("La suppression du dossier a échoué.",
				   "message box content"));
		}
	}
}

/**
	@brief ElementsCollectionWidget::editDirectory
	Edit the directory represented by the current selected item
*/
void ElementsCollectionWidget::editDirectory()
{
	ElementCollectionItem *eci = elementCollectionItemForIndex(
				m_index_at_context_menu);

	if (eci->type() != FileElementCollectionItem::Type) return;

	FileElementCollectionItem *feci =
			static_cast<FileElementCollectionItem*>(eci);
	if(feci->isCommonCollection()) return;

	ElementsLocation location(feci->collectionPath());
	ElementsCategoryEditor ece(location, true, this);

	if (ece.exec() == QDialog::Accepted)
		eci->clearData();
}

/**
	@brief ElementsCollectionWidget::newDirectory
	Create a new directory
*/
void ElementsCollectionWidget::newDirectory()
{
	ElementCollectionItem *eci = elementCollectionItemForIndex(m_index_at_context_menu);

	if (!eci || eci->type() != FileElementCollectionItem::Type) return;

	FileElementCollectionItem *feci = static_cast<FileElementCollectionItem*>(eci);
	if(feci->isCommonCollection()) return;

	ElementsLocation location(feci->collectionPath());
	ElementsCategoryEditor new_dir_editor(location, false, this);

	if (new_dir_editor.exec() == QDialog::Accepted) {
		ElementsLocation new_loc = new_dir_editor.createdLocation();

		if (new_loc.isMacrosCollection()) {
			if (m_macros_model) {
				m_macros_model->addLocation(new_loc);
			}
		} else {
			if (m_model) {
				m_model->addLocation(new_loc);
			}
		}
	}
}

/**
	@brief ElementsCollectionWidget::newElement
	Create a new element.
*/
void ElementsCollectionWidget::newElement()
{
	ElementCollectionItem *eci = elementCollectionItemForIndex(
				m_index_at_context_menu);

	if (eci->type() != FileElementCollectionItem::Type) {
		return;
	}

	FileElementCollectionItem *feci =
			static_cast<FileElementCollectionItem*>(eci);
	if(feci->isCommonCollection()) {
		return;
	}

	NewElementWizard elmt_wizard(this);
	ElementsLocation loc(feci->collectionPath());
	elmt_wizard.preselectedLocation(loc);
	elmt_wizard.exec();

	foreach (QETElementEditor *element_editor,
			 QETApp::instance()->elementEditors())
		connect(element_editor,
			&QETElementEditor::saveToLocation,
			this,
			&ElementsCollectionWidget::locationWasSaved);
}

/**
	@brief ElementsCollectionWidget::showThisDir
	Hide all directories except the pointed dir;
*/
void ElementsCollectionWidget::showThisDir()
{
		//Disable the yellow background of the previous index
	if (m_showed_index.isValid())
	{
		ElementCollectionItem *eci =
				elementCollectionItemForIndex(m_showed_index);
		if (eci)
			eci->setBackground(QBrush());
	}

	m_showed_index = m_index_at_context_menu;
	if (m_showed_index.isValid())
	{
		hideCollection(true);
		showAndExpandItem(m_showed_index, true, true);
		ElementCollectionItem *eci =
				elementCollectionItemForIndex(m_showed_index);
		if (eci)
			eci->setBackground(QBrush(QColor(255, 204, 0, 255)));
		search();
	}
	else
		resetShowThisDir();
}

/**
	@brief ElementsCollectionWidget::resetShowThisDir
	reset show this dir, all collection are show.
	If search field isn't empty, apply the search after show all collection
*/
void ElementsCollectionWidget::resetShowThisDir()
{
	if (m_showed_index.isValid())
	{
		ElementCollectionItem *eci = elementCollectionItemForIndex(
					m_showed_index);
		if (eci)
			eci->setBackground(QBrush());
	}

	m_showed_index = QModelIndex();
	search();
}

/**
	@brief ElementsCollectionWidget::dirProperties
	Open an informative dialog about the current index
*/
void ElementsCollectionWidget::dirProperties()
{
	ElementCollectionItem* eci =
		elementCollectionItemForIndex(m_index_at_context_menu);

	if (eci && eci->isDir())
	{
		QString filePath;
		if (eci->type() == FileElementCollectionItem::Type) {
			filePath = tr("Chemin dans le système de fichiers :  %1")
						   .arg(
							   static_cast<FileElementCollectionItem*>(eci)
								   ->fileSystemPath());
		}
		QString out =
			tr("Le dossier %1 contient").arg(eci->localName()) % " "
			% tr("%n élément(s), répartie(s)", "", eci->elementsChild().size())
			% " "
			% tr("dans %n dossier(s).", "", eci->directoriesChild().size())
			% "\n\n"
			% tr("Chemin de la collection :  %1").arg(eci->collectionPath())
			% "\n" % filePath;
		qInfo() << out;
		QMessageBox::information(
			this,
			tr("Propriété du dossier %1").arg(eci->localName()),
			out);
	}
}

/**
	@brief ElementsCollectionWidget::reload, the displayed collections.
*/
void ElementsCollectionWidget::reload()
{
	m_loading_timer.reset(new QElapsedTimer());
	qInfo()<<"Elements collection reload";
	m_loading_timer->start();

	m_progress_bar->show();
	// Force to repaint now,
	// else progress bar will be not displayed immediately
	m_progress_bar->setValue(1);
	m_tree_view->setDisabled(true);
	// Force to repaint now,
	// else tree view will be not disabled immediately
	m_tree_view->repaint();
	m_progress_bar->setFormat(QObject::tr("chargement %p% (%v sur %m)"));
	
	QList <QETProject *> project_list;
	project_list.append(m_waiting_project);
	m_waiting_project.clear();
	if (m_model)
		project_list.append(m_model->project());

	if(m_new_model) {
		m_new_model->deleteLater();
	}
	m_new_model = new ElementsCollectionModel(m_tree_view);
	connect(m_new_model,
		&ElementsCollectionModel::loadingProgressRangeChanged,
		m_progress_bar,
		&QProgressBar::setRange);
	connect(m_new_model,
		&ElementsCollectionModel::loadingProgressValueChanged,
		m_progress_bar,
		&QProgressBar::setValue);
	connect(m_new_model,
		&ElementsCollectionModel::loadingFinished,
		this,
		&ElementsCollectionWidget::loadingFinished);

	m_new_model->loadCollections(true, true, true, project_list);

	if (m_macros_model) {
		m_macros_model->deleteLater();
	}
	m_macros_model = new ElementsCollectionModel(m_macros_tree_view);
	m_macros_tree_view->setModel(m_macros_model);
	m_macros_model->loadMacrosCollection();
}

/**
 * @brief ElementsCollectionWidget::loadingFinished
 * Process when collection finished to be loaded
 */
void ElementsCollectionWidget::loadingFinished()
{
	if (m_new_model)
	{
		m_new_model->highlightUnusedElement();
		m_tree_view->setModel(m_new_model);
		m_grid_view->setModel(m_new_model);
		m_grid_view->setRootIndex(QModelIndex());
		m_index_at_context_menu = QModelIndex();
		m_showed_index = QModelIndex();
		if (m_model) delete m_model;
		m_model = m_new_model;
		m_new_model = nullptr;
		expandFirstItems();
	}
	else {
		m_model->highlightUnusedElement();
	}

	m_progress_bar->hide();
	m_tree_view->setEnabled(true);

	if (m_loading_timer) {
		qInfo()<<"Elements collection finished to be loaded in" << m_loading_timer->elapsed()/1000.0 << "seconds";
		m_loading_timer.reset();
	}
	else {
		qInfo()<<"Elements collection finished to be loaded";
	}
}

/**
	@brief ElementsCollectionWidget::locationWasSaved
	This method is connected with the signal savedToLocation
	of Element editor (see ElementsCollectionWidget::editElement())
	Update or add the item represented by location to m_model
	@param location
*/
void ElementsCollectionWidget::locationWasSaved(
		const ElementsLocation& location)
{
	//Because this method update an item in the model, location must
	//represent an existing element (in file system of project)
	if (!location.exist())
		return;

	QModelIndex index = m_model->indexFromLocation(location);

	if (index.isValid()) {
		QStandardItem *item = m_model->itemFromIndex(index);
		if (item) {
			static_cast<ElementCollectionItem *>(item)->clearData();
			static_cast<ElementCollectionItem *>(item)->setUpData();
		}
	}
	else {
		m_model->addLocation(location);
	}
}

/**
	@brief ElementsCollectionWidget::search
	Search every item (directory or element)
	that match the text of m_search_field and display it,
	other item who does not match text is hidden
*/
void ElementsCollectionWidget::search()
{
	QString text = m_search_field->text();
		//Reset the search
	if (text.isEmpty())
	{
		QModelIndex current_index = m_tree_view->currentIndex();
		m_tree_view->reset();

		if (m_showed_index.isValid())
		{
			hideCollection(true);
			showAndExpandItem(m_showed_index, true, true);
		}
		else
			expandFirstItems();

		//Expand the tree and scroll to the last selected index
		if (current_index.isValid())
		{
			showAndExpandItem(current_index);
			m_tree_view->setCurrentIndex(current_index);
			m_tree_view->scrollTo(current_index);
		}
		return;
	}

		//start the search when text have at least 3 letters.
	if (text.count() < 3) {
		return;
	}

	hideCollection(true);

	const QStringList text_list = text.split("+", Qt::SkipEmptyParts);
	QModelIndexList match_index;
	for (QString txt : text_list) {
		match_index << m_model->match(m_showed_index.isValid()
						  ? m_model->index(0,0,m_showed_index)
						  : m_model->index(0,0),
						  Qt::UserRole+1,
						  QVariant(txt),
						  -1,
						  Qt::MatchContains
						  | Qt::MatchRecursive);
	}

	for(QModelIndex index : match_index)
		showAndExpandItem(index);
}

/**
	@brief ElementsCollectionWidget::hideCollection
	Hide all collection displayed in this tree
	@param hide- true = hide , false = visible
*/
void ElementsCollectionWidget::hideCollection(bool hide)
{
	for (int i=0 ; i <m_model->rowCount() ; i++)
		hideItem(hide, m_model->index(i, 0), true);
}

/**
	@brief ElementsCollectionWidget::hideItem
	Hide the item index. If recursive is true,
	hide all subchilds of index
	@param hide : - true = hide , false = visible
	@param index : - index to hide
	@param recursive : - true = apply to child , false = only for index
*/
void ElementsCollectionWidget::hideItem(bool hide,
					const QModelIndex &index,
					bool recursive)
{
	m_tree_view->setRowHidden(index.row(), index.parent(), hide);

	if (recursive)
		for (int i=0 ; i<m_model->rowCount(index) ; i++)
			hideItem(hide, m_model->index(i, 0, index), recursive);
}

/**
	@brief ElementsCollectionWidget::showAndExpandItem
	Show the item index and expand it.
	If parent is true, ensure parents of index is show and expanded
	If child is true, ensure all childs of index is show and expended
	@param index- index to show
	@param parent- Apply to parent
	@param child- Apply to all childs
*/
void ElementsCollectionWidget::showAndExpandItem(const QModelIndex &index,
						 bool parent,
						 bool child)
{
	if (index.isValid()) {
		if (parent)
			showAndExpandItem(index.parent(), parent);

		hideItem(false, index, child);
		m_tree_view->expand(index);
	}
}

/**
 * @brief ElementsCollectionWidget::elementCollectionItemForIndex
 * @param index
 * @return The internal pointer of index casted to ElementCollectionItem;
 */
ElementCollectionItem *ElementsCollectionWidget::elementCollectionItemForIndex(const QModelIndex &index)
{
	if (!index.isValid()) return nullptr;

	if (m_macros_model && index.model() == m_macros_model) {
		return static_cast<ElementCollectionItem *>(m_macros_model->itemFromIndex(index));
	}

	if (m_model && index.model() == m_model) {
		return static_cast<ElementCollectionItem *>(m_model->itemFromIndex(index));
	}

	return nullptr;
}
