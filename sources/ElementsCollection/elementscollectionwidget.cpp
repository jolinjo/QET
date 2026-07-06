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
#include <QEventLoop>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QPicture>
#include <QStyledItemDelegate>
#include <functional>
#include <QSettings>
#include <QSplitter>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTreeWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QFile>
#include <QHash>
#include <QSet>
#include <QLabel>

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

		/* le cadre n'entoure que l'icone, le libelle est dessine
		 * sous le cadre ; la geometrie part de la case reellement
		 * disponible pour rester coherente quelle que soit la
		 * combinaison taille d'icone / largeur de case configuree */
		const bool selected = option.state & QStyle::State_Selected;
		const int text_h = option.fontMetrics.height() * 2 + 4;
		const QRect box(cell.left(), cell.top(), cell.width(),
				qMax(16, cell.height() - text_h));

		painter->fillRect(box, selected
			? option.palette.highlight()
			: option.palette.base());
		painter->setPen(QColor(0xc8, 0xc8, 0xc8));
		painter->drawRect(box.adjusted(0, 0, -1, -1));

		const int icon_target = qMax(8, qMin(m_icon_size,
			qMin(box.width() - 6, box.height() - 6)));
		const QString path = m_path_for_index(index);
		QPixmap pixmap;
		if (!path.isEmpty()
		    && path.endsWith(QLatin1String(".elmt"))) {
			pixmap = elementPixmap(
				path, icon_target,
				option.widget
					? option.widget->devicePixelRatioF()
					: 1.0);
		}
		if (!pixmap.isNull()) {
			const QSizeF logical =
				pixmap.deviceIndependentSize();
			painter->drawPixmap(
				box.center()
					- QPoint(logical.width() / 2,
						 logical.height() / 2),
				pixmap);
		} else {
			index.data(Qt::DecorationRole).value<QIcon>().paint(
				painter,
				box.adjusted(3, 3, -3, -3),
				Qt::AlignCenter);
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

	QSize sizeHint(const QStyleOptionViewItem &option,
		       const QModelIndex &) const override
	{
		return QSize(m_icon_size + 28,
			     m_icon_size
				     + option.fontMetrics.height() * 2 + 20);
	}

	private:
	QPixmap elementPixmap(const QString &path, int size, qreal dpr) const
	{
		const QString key = path + QChar('@')
				    + QString::number(size)
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
				(size - margin) / qreal(bounding.width()),
				(size - margin) / qreal(bounding.height()));
			pixmap = QPixmap(QSize(size, size) * dpr);
			pixmap.setDevicePixelRatio(dpr);
			pixmap.fill(Qt::transparent);
			QPainter p(&pixmap);
			p.setRenderHint(QPainter::Antialiasing);
			p.translate(QPointF(size / 2.0, size / 2.0)
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

namespace
{
	/* lance un processus et attend sa fin sans bloquer l'interface */
	bool run_process(const QString &program, const QStringList &arguments,
			 const QString &working_dir, QString *output)
	{
		QProcess process;
		if (!working_dir.isEmpty()) {
			process.setWorkingDirectory(working_dir);
		}
		process.setProcessChannelMode(QProcess::MergedChannels);
		process.start(program, arguments);
		if (!process.waitForStarted()) {
			if (output) {
				*output = program + QStringLiteral(" : ")
					  + process.errorString();
			}
			return false;
		}
		QEventLoop loop;
		QObject::connect(
			&process,
			QOverload<int, QProcess::ExitStatus>::of(
				&QProcess::finished),
			&loop, &QEventLoop::quit);
		loop.exec();
		if (output) {
			*output = QString::fromUtf8(process.readAll());
		}
		return process.exitStatus() == QProcess::NormalExit
		       && process.exitCode() == 0;
	}
}

/**
	Download (shallow git clone) the company libraries repository and
	mirror its collections into the QET data directory, with a tar.gz
	backup of the current content beforehand.
*/
namespace {
	/// Extract a "vX[.Y[.Z]]" version token from a string, or empty.
	QString qetlib_version(const QString &s) {
		static const QRegularExpression re(
			QStringLiteral("v\\d+(?:\\.\\d+)*"));
		const QRegularExpressionMatch m = re.match(s);
		return m.hasMatch() ? m.captured(0) : QString();
	}

	/// Preferred display name (zh_TW > zh > en > any) from qet_directory XML text.
	QString qetlib_name_from_xml(const QString &xml_text) {
		QXmlStreamReader xml(xml_text);
		QHash<QString, QString> names;
		while (!xml.atEnd()) {
			if (xml.readNext() == QXmlStreamReader::StartElement
			    && xml.name() == QLatin1String("name")) {
				const QString lang = xml.attributes()
					.value(QLatin1String("lang")).toString();
				names.insert(lang, xml.readElementText());
			}
		}
		for (const QString &l : { QStringLiteral("zh_TW"),
					  QStringLiteral("zh"),
					  QStringLiteral("en") }) {
			if (names.contains(l)) return names.value(l);
		}
		return names.isEmpty() ? QString() : *names.constBegin();
	}

	/// Preferred display name from a qet_directory file on disk.
	QString qetlib_dir_name(const QString &qet_directory_path) {
		QFile f(qet_directory_path);
		if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
		return qetlib_name_from_xml(QString::fromUtf8(f.readAll()));
	}

	/// Highest version token among the *.titleblock files of a directory.
	QString qetlib_titleblocks_version(const QString &dir) {
		QString best;
		const QFileInfoList list = QDir(dir).entryInfoList(
			QStringList { QStringLiteral("*.titleblock") }, QDir::Files);
		for (const QFileInfo &fi : list) {
			const QString v = qetlib_version(fi.fileName());
			if (v > best) best = v;
		}
		return best;
	}

	/// Recursive copy of a directory content, skipping .git.
	bool qetlib_copy_recursive(const QString &src, const QString &dst) {
		QDir sdir(src);
		if (!sdir.exists()) return false;
		if (!QDir().mkpath(dst)) return false;
		const QFileInfoList entries = sdir.entryInfoList(
			QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
		for (const QFileInfo &fi : entries) {
			if (fi.fileName() == QLatin1String(".git")) continue;
			const QString target = dst % QChar('/') % fi.fileName();
			if (fi.isDir()) {
				if (!qetlib_copy_recursive(fi.absoluteFilePath(), target))
					return false;
			} else if (!QFile::copy(fi.absoluteFilePath(), target)) {
				return false;
			}
		}
		return true;
	}

	/// Full mirror: replace dst with the content of src (cross-platform, no rsync).
	bool qetlib_mirror(const QString &src, const QString &dst) {
		QDir(dst).removeRecursively();
		return qetlib_copy_recursive(src, dst);
	}
}

/**
	Fetch the company-library repository (configured in the preferences),
	list each library (element collections + title blocks) with its online
	and local version, and let the user pick which ones to mirror into the
	QET data directory. The current content is backed up (tar.gz) beforehand.
*/
void ElementsCollectionWidget::updateLibraryFromGit()
{
	QSettings settings;
	const QString url = settings.value(
		QStringLiteral("elementspanel/library-git-url"),
		QStringLiteral("https://github.com/jolinjo/QET-Lib"))
		.toString().trimmed();
	if (url.isEmpty()) return;

	const QString data_dir = QETApp::dataDir();
	const QString cache_dir =
		data_dir % QStringLiteral("/library-git-cache");

		//inline progress shown right under the「更新公司庫」button (no popup dialog)
	auto sync_begin = [this](const QString &text) {
		m_sync_status->setText(text);
		m_sync_progress->setRange(0, 0);   // indeterminate (busy)
		m_sync_status->show();
		m_sync_progress->show();
		QCoreApplication::processEvents();
	};
	auto sync_text  = [this](const QString &text) {
		m_sync_status->setText(text);
		QCoreApplication::processEvents();
	};
	auto sync_range = [this](int mn, int mx) { m_sync_progress->setRange(mn, mx); };
	auto sync_value = [this](int v) {
		m_sync_progress->setValue(v);
		QCoreApplication::processEvents();
	};
	auto sync_end   = [this]() {
		m_sync_status->hide();
		m_sync_progress->hide();
	};

		//1) fetch only the repository metadata (blobless, no working files) so
		//   listing is fast : file contents are downloaded later, on demand, only
		//   for the libraries the user actually updates.
	sync_begin(tr("讀取線上公司庫…"));
	QDir(cache_dir).removeRecursively();
	QString log;
	// core.longpaths=true : the company repo has very deep paths that exceed the
	// Windows 260-char MAX_PATH; blob:none + no-checkout : metadata-only clone.
	if (!run_process(QStringLiteral("git"),
			 { QStringLiteral("-c"), QStringLiteral("core.longpaths=true"),
			   QStringLiteral("clone"), QStringLiteral("--depth"),
			   QStringLiteral("1"), QStringLiteral("--filter=blob:none"),
			   QStringLiteral("--no-checkout"), QStringLiteral("--single-branch"),
			   url, cache_dir }, QString(), &log)) {
		sync_end();
		QMessageBox::warning(this, tr("更新公司庫"),
			tr("讀取線上倉庫失敗：\n%1").arg(log.right(1500)));
		return;
	}
	// keep the dialog up : the version comparison below fetches a few blobs on
	// demand over the network, which would otherwise look like a frozen UI.
	sync_text(tr("比對線上與本機版本…"));
	QCoreApplication::processEvents();

		//2) enumerate libraries + online/local versions from the git tree
	struct LibItem {
		QString kind;       // "elements" | "titleblocks"
		QString repo_sub;
		QString target_sub;
		QString display;
		QString online_ver;
		QString local_ver;
	};
	QList<LibItem> items;

	// element collections : each direct subdir of elements-company that has a
	// qet_directory. ls-tree lists the tree entries; git show reads the file.
	QString elem_tree;
	run_process(QStringLiteral("git"),
		{ QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
		  QStringLiteral("ls-tree"), QStringLiteral("-d"),
		  QStringLiteral("--name-only"), QStringLiteral("HEAD"),
		  QStringLiteral("elements-company/") }, cache_dir, &elem_tree);
	const QStringList elem_paths = elem_tree.split(QChar('\n'), Qt::SkipEmptyParts);
	sync_range(0, elem_paths.count() + 1);
	int fetch_done = 0;
	for (const QString &path : elem_paths) {
		sync_value(fetch_done++);
		QCoreApplication::processEvents();
		const QString sub = path.section(QChar('/'), -1).trimmed();
		if (sub.isEmpty()) continue;
		QString qd_xml;
		if (!run_process(QStringLiteral("git"),
			{ QStringLiteral("show"),
			  QStringLiteral("HEAD:elements-company/") % sub
				  % QStringLiteral("/qet_directory") },
			cache_dir, &qd_xml)) {
			continue; // no qet_directory in this subdir : not a library
		}
		LibItem it;
		it.kind = QStringLiteral("elements");
		it.repo_sub = QStringLiteral("elements-company/") % sub;
		it.target_sub = it.repo_sub;
		it.display = qetlib_name_from_xml(qd_xml);
		if (it.display.isEmpty()) it.display = sub;
		it.online_ver = qetlib_version(it.display);
		const QString local_qd = data_dir % QChar('/') % it.target_sub
			% QStringLiteral("/qet_directory");
		it.local_ver = QFileInfo::exists(local_qd)
			? qetlib_version(qetlib_dir_name(local_qd)) : QString();
		items << it;
	}

	// company title blocks : the whole titleblocks-company, versioned by filename
	QString tb_tree;
	if (run_process(QStringLiteral("git"),
		{ QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
		  QStringLiteral("ls-tree"), QStringLiteral("--name-only"),
		  QStringLiteral("HEAD"), QStringLiteral("titleblocks-company/") },
		cache_dir, &tb_tree)
	    && !tb_tree.trimmed().isEmpty()) {
		QString online_ver;
		const QStringList tb_files = tb_tree.split(QChar('\n'), Qt::SkipEmptyParts);
		for (const QString &f : tb_files) {
			const QString v = qetlib_version(f.section(QChar('/'), -1));
			if (v > online_ver) online_ver = v;
		}
		LibItem it;
		it.kind = QStringLiteral("titleblocks");
		it.repo_sub = QStringLiteral("titleblocks-company");
		it.target_sub = it.repo_sub;
		it.display = tr("公司圖框");
		it.online_ver = online_ver;
		it.local_ver = qetlib_titleblocks_version(
			data_dir % QStringLiteral("/titleblocks-company"));
		items << it;
	}

	sync_value(m_sync_progress->maximum());
	sync_end();

	if (items.isEmpty()) {
		QMessageBox::information(this, tr("更新公司庫"),
			tr("線上倉庫沒有可更新的公司庫。"));
		return;
	}

		//3) selection dialog
	QDialog dlg(this);
	dlg.setWindowTitle(tr("線上更新公司庫"));
	dlg.resize(560, 420);
	auto *vl = new QVBoxLayout(&dlg);
	vl->addWidget(new QLabel(
		tr("勾選要更新的項目（以線上版本完全覆蓋本機）："), &dlg));
	auto *tree = new QTreeWidget(&dlg);
	tree->setColumnCount(4);
	tree->setHeaderLabels({ tr("庫"), tr("線上版本"),
				tr("本機版本"), tr("狀態") });
	tree->setRootIsDecorated(false);
	tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	vl->addWidget(tree);

	QList<QPair<QTreeWidgetItem *, LibItem>> rows;
	const QString up_to_date = tr("最新");
	auto add_group = [&](const QString &title, const QString &kind) {
		auto *head = new QTreeWidgetItem(tree, { title });
		QFont bf = head->font(0);
		bf.setBold(true);
		head->setFont(0, bf);
		head->setFlags(Qt::ItemIsEnabled);
		bool any = false;
		for (const LibItem &it : items) {
			if (it.kind != kind) continue;
			any = true;
			const QString status = it.local_ver.isEmpty()
				? tr("未安裝")
				: (it.online_ver != it.local_ver ? tr("可更新")
								 : up_to_date);
			auto *row = new QTreeWidgetItem(head, {
				it.display,
				it.online_ver.isEmpty() ? QStringLiteral("—") : it.online_ver,
				it.local_ver.isEmpty() ? QStringLiteral("—") : it.local_ver,
				status });
			row->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
			row->setCheckState(0, status == up_to_date
				? Qt::Unchecked : Qt::Checked);
			rows.append({ row, it });
		}
		if (!any) delete head;
	};
	add_group(tr("元件庫"), QStringLiteral("elements"));
	add_group(tr("圖框"), QStringLiteral("titleblocks"));
	tree->expandAll();
	for (int c = 1 ; c < 4 ; ++c) tree->resizeColumnToContents(c);

	auto *bb = new QDialogButtonBox(&dlg);
	bb->addButton(tr("更新選取"), QDialogButtonBox::AcceptRole);
	bb->addButton(QDialogButtonBox::Cancel);
	connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	vl->addWidget(bb);

	if (dlg.exec() != QDialog::Accepted) return;

	QList<LibItem> selected;
	for (const auto &pair : rows) {
		if (pair.first->checkState(0) == Qt::Checked)
			selected << pair.second;
	}
	if (selected.isEmpty()) return;

		//4) download the selection, then mirror it into the data dir
	sync_begin(tr("下載選取的庫…"));
	sync_range(0, selected.count() + 1);
	sync_value(0);

	// download (checkout) only the selected paths from the metadata-only clone
	QStringList checkout_paths;
	for (const LibItem &it : selected) checkout_paths << it.repo_sub;
	run_process(QStringLiteral("git"),
		{ QStringLiteral("sparse-checkout"), QStringLiteral("init"),
		  QStringLiteral("--no-cone") }, cache_dir, &log);
	if (!run_process(QStringLiteral("git"),
		QStringList { QStringLiteral("-c"), QStringLiteral("core.longpaths=true"),
		  QStringLiteral("sparse-checkout"), QStringLiteral("set"),
		  QStringLiteral("--no-cone") } + checkout_paths, cache_dir, &log)
	    || !run_process(QStringLiteral("git"),
		{ QStringLiteral("-c"), QStringLiteral("core.longpaths=true"),
		  QStringLiteral("checkout") }, cache_dir, &log)) {
		sync_end();
		QMessageBox::warning(this, tr("更新公司庫"),
			tr("下載選取的庫失敗：\n%1").arg(log.right(1500)));
		return;
	}

	sync_value(1);
	sync_text(tr("套用更新…"));
	QCoreApplication::processEvents();

	QStringList updated;
	int mirror_done = 1;
	for (const LibItem &it : selected) {
		const QString source = cache_dir % QChar('/') % it.repo_sub;
		const QString target = data_dir % QChar('/') % it.target_sub;
		if (!QFileInfo::exists(source)) continue;
		if (!qetlib_mirror(source, target)) {
			sync_end();
			QMessageBox::warning(this, tr("更新公司庫"),
				tr("更新「%1」失敗。").arg(it.display));
			return;
		}
		updated << (it.online_ver.isEmpty()
			? it.display
			: it.display % QChar(' ') % it.online_ver);
		sync_value(++mirror_done);
		QCoreApplication::processEvents();
	}
	// remove the metadata cache : it is only needed during this operation and
	// leaving a nested git repo under the data dir just pollutes it.
	QDir(cache_dir).removeRecursively();
	sync_end();

	QMessageBox::information(this, tr("更新公司庫"),
		tr("已更新：\n%1").arg(updated.join(QChar('\n'))));

	// drop the cached element pictures so updated elements are re-rendered
	// (they are keyed by UUID, so a file change alone would keep the old image).
	ElementPictureFactory::dropInstance();
	reload();
	if (QETDiagramEditor *editor = QETApp::diagramEditorAncestorOf(this)) {
		editor->reloadOldElementPanel();
	}
}

/**
	Hide every element row of the tree : elements are browsed in the
	grid view below, the tree only shows the directory structure.
*/
void ElementsCollectionWidget::hideElementRows(const QModelIndex &parent)
{
	if (!m_model) return;
	const int count = m_model->rowCount(parent);
	for (int row = 0 ; row < count ; ++row) {
		const QModelIndex index = m_model->index(row, 0, parent);
		ElementCollectionItem *eci = elementCollectionItemForIndex(index);
		if (eci && eci->isElement()) {
			m_tree_view->setRowHidden(row, parent, true);
		} else {
			hideElementRows(index);
		}
	}
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
	m_update_library = new QAction(QET::Icons::QETDownload,
				       tr("Mettre à jour les collections depuis GitHub..."),
				       this);
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

	auto *update_lib_btn = new QPushButton(tr("更新公司庫…", "button"), this);
	update_lib_btn->setToolTip(tr("從線上倉庫更新公司元件庫與圖框"));
	connect(update_lib_btn, &QPushButton::clicked,
		this, &ElementsCollectionWidget::updateLibraryFromGit);
	m_main_vlayout->addWidget(update_lib_btn);

		//更新公司庫的進度：直接內嵌在更新按鈕下方（狀態文字 + 進度條），不另跳對話框
	m_sync_status = new QLabel(this);
	m_sync_status->setWordWrap(true);
	m_sync_status->hide();
	m_main_vlayout->addWidget(m_sync_status);
	m_sync_progress = new QProgressBar(this);
	m_sync_progress->hide();
	m_main_vlayout->addWidget(m_sync_progress);

	m_main_vlayout->addWidget(m_search_field);
	m_main_vlayout->addWidget(m_tab_widget);

	m_progress_bar = new QProgressBar(this);
	m_progress_bar->setFormat(QObject::tr("chargement %p% (%v sur %m)"));
	m_main_vlayout->addWidget(m_progress_bar);
	m_progress_bar->hide();

	m_context_menu = new QMenu(this);

		//Apply the configured library font size to every view
	QSettings lib_settings;
	const int lib_base = m_tree_view->font().pointSize() > 0
			? m_tree_view->font().pointSize() : 9;
	QFont lib_font = m_tree_view->font();
	lib_font.setPointSize(lib_settings.value("fontsize_library", lib_base).toInt());
	applyLibraryFont(lib_font);
}

/**
	@brief ElementsCollectionWidget::applyLibraryFont
	Apply a font (size) to all element/macros views of the collection panel.
*/
void ElementsCollectionWidget::applyLibraryFont(const QFont &font)
{
	// Apply to the widget and every descendant (tree/grid/macros views, search
	// field, tabs) — none of them reliably inherit the dock font.
	setFont(font);
	const QList<QWidget *> kids = findChildren<QWidget *>();
	for (QWidget *k : kids) {
		k->setFont(font);
	}
	if (m_grid_view) updateGridGeometry();
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
	connect(m_update_library, &QAction::triggered,
		this, &ElementsCollectionWidget::updateLibraryFromGit);

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
	m_context_menu->addAction(m_update_library);

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

	const bool show_common = !QSettings().value(
		QStringLiteral("collections/hide-common-elements"), false).toBool();
	m_new_model->loadCollections(show_common, true, true, project_list);

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
		hideElementRows();
		/* les elements ajoutes apres coup (projets ouverts, imports)
		 * doivent aussi rester caches dans l'arbre */
		connect(m_model, &QStandardItemModel::rowsInserted, this,
			[this](const QModelIndex &parent, int first, int last) {
			/* borner le travail aux lignes reellement inserees :
			 * re-balayer tout le sous-arbre du parent a chaque
			 * insertion devenait quadratique lors des integrations
			 * en serie (collage inter-projets) */
			for (int row = first ; row <= last ; ++row) {
				const QModelIndex index =
					m_model->index(row, 0, parent);
				ElementCollectionItem *eci =
					elementCollectionItemForIndex(index);
				if (!eci) continue;
				if (eci->isElement()) {
					m_tree_view->setRowHidden(
						row, parent, true);
				} else {
					hideElementRows(index);
				}
			}
		});
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
		hideElementRows();
		return;
	}

		//start the search when text have at least 3 letters (latin).
		//CJK 詞彙很短（常 1-2 字），含 CJK 字元時放寬到 1 字即可搜尋。
	bool has_cjk = false;
	for (const QChar &c : text) {
		if (c.unicode() >= 0x2E80) { has_cjk = true; break; }
	}
	if (!has_cjk && text.count() < 3) {
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
