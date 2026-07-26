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
#include "pdfviewerwindow.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPdfBookmarkModel>
#include <QPdfDocument>
#include <QPdfPageNavigator>
#include <QPdfView>
#include <QSettings>
#include <QSplitter>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

PdfViewerWindow::PdfViewerWindow(QWidget *parent) :
	QWidget(parent)
{
	setWindowTitle(tr("PDF 檢視"));

	// 按鈕列字型比照主工具列(介面字型 + fontsize_toolbar 設定),避免比主
	// 視窗字型小或不一致。
	QFont ui_font = qApp->font();
	{
		const int base = ui_font.pointSize() > 0 ? ui_font.pointSize() : 9;
		ui_font.setPointSize(
			QSettings().value(QStringLiteral("fontsize_toolbar"),
					  base).toInt());
	}

	m_doc = new QPdfDocument(this);
	m_view = new QPdfView(this);
	m_view->setDocument(m_doc);
	m_view->setPageMode(QPdfView::PageMode::MultiPage);
	m_view->setZoomMode(QPdfView::ZoomMode::FitToWidth);

	// 頂部工具列(按鈕列)+ 下方 QPdfView
	auto *bar = new QHBoxLayout();
	bar->setContentsMargins(4, 4, 4, 4);
	bar->setSpacing(4);
	auto addBtn = [this, bar, ui_font](const QString &text,
					   void (PdfViewerWindow::*fn)()) {
		auto *b = new QToolButton(this);
		b->setText(text);
		b->setAutoRaise(true);
		b->setFont(ui_font);
		connect(b, &QToolButton::clicked, this, fn);
		bar->addWidget(b);
		return b;
	};
	addBtn(tr("開啟…"), &PdfViewerWindow::openFileDialog);
	bar->addSpacing(8);
	addBtn(tr("上一頁"), &PdfViewerWindow::prevPage);
	addBtn(tr("下一頁"), &PdfViewerWindow::nextPage);
	m_page_label = new QLabel(QStringLiteral("  –  "), this);
	m_page_label->setFont(ui_font);
	bar->addWidget(m_page_label);
	bar->addSpacing(8);
	addBtn(tr("放大"), &PdfViewerWindow::zoomIn);
	addBtn(tr("縮小"), &PdfViewerWindow::zoomOut);
	addBtn(tr("適合寬度"), &PdfViewerWindow::fitWidth);
	addBtn(tr("整頁顯示"), &PdfViewerWindow::fitPage);
	bar->addStretch();

	// 左側書籤/目錄(PDF 大綱),右側 PDF 檢視;有書籤才顯示左側
	m_bmodel = new QPdfBookmarkModel(this);
	m_bmodel->setDocument(m_doc);
	m_bookmarks = new QTreeView(this);
	m_bookmarks->setModel(m_bmodel);
	m_bookmarks->setHeaderHidden(true);
	m_bookmarks->setFont(ui_font);
	m_bookmarks->hide();
	connect(m_bookmarks, &QTreeView::activated,
		this, &PdfViewerWindow::bookmarkActivated);
	connect(m_bookmarks, &QTreeView::clicked,
		this, &PdfViewerWindow::bookmarkActivated);
	// 書籤模型於文件載入後可能非同步重建,重建時再更新左側面板顯示
	connect(m_bmodel, &QAbstractItemModel::modelReset,
		this, &PdfViewerWindow::refreshBookmarks);

	auto *split = new QSplitter(Qt::Horizontal, this);
	split->addWidget(m_bookmarks);
	split->addWidget(m_view);
	split->setStretchFactor(0, 0);
	split->setStretchFactor(1, 1);
	split->setSizes({240, 760});

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(0);
	root->addLayout(bar);
	root->addWidget(split, 1);

	connect(m_view->pageNavigator(), &QPdfPageNavigator::currentPageChanged,
		this, [this](int) { updatePageLabel(); });
	updatePageLabel();
}

PdfViewerWindow::~PdfViewerWindow() = default;

bool PdfViewerWindow::openFile(const QString &path)
{
	if (path.isEmpty()) return false;
	const QPdfDocument::Error err = m_doc->load(path);
	if (err != QPdfDocument::Error::None || m_doc->pageCount() < 1) {
		QMessageBox::warning(this, tr("PDF 檢視"),
			tr("無法開啟 PDF:%1").arg(path));
		return false;
	}
	setWindowTitle(tr("PDF 檢視 — %1").arg(QFileInfo(path).fileName()));
	m_view->pageNavigator()->jump(0, QPointF(), m_view->zoomFactor());
	updatePageLabel();
	refreshBookmarks();
	return true;
}

void PdfViewerWindow::refreshBookmarks()
{
	if (!m_bmodel || !m_bookmarks) return;
	const bool has = m_bmodel->rowCount(QModelIndex()) > 0;
	m_bookmarks->setVisible(has);
	if (has) m_bookmarks->expandToDepth(0);
}

void PdfViewerWindow::bookmarkActivated(const QModelIndex &index)
{
	if (!index.isValid()) return;
	const int page = index.data(
		int(QPdfBookmarkModel::Role::Page)).toInt();
	if (page >= 0)
		m_view->pageNavigator()->jump(page, QPointF(),
					      m_view->zoomFactor());
}

void PdfViewerWindow::fitPage()
{
	m_view->setZoomMode(QPdfView::ZoomMode::FitInView);
}

void PdfViewerWindow::openFileDialog()
{
	const QString path = QFileDialog::getOpenFileName(this,
		tr("開啟 PDF"), QString(), tr("PDF 檔 (*.pdf)"));
	if (!path.isEmpty()) openFile(path);
}

void PdfViewerWindow::prevPage()
{
	auto *nav = m_view->pageNavigator();
	const int p = nav->currentPage();
	if (p > 0) nav->jump(p - 1, QPointF(), m_view->zoomFactor());
}

void PdfViewerWindow::nextPage()
{
	auto *nav = m_view->pageNavigator();
	const int p = nav->currentPage();
	if (p < m_doc->pageCount() - 1)
		nav->jump(p + 1, QPointF(), m_view->zoomFactor());
}

void PdfViewerWindow::zoomIn()
{
	m_view->setZoomMode(QPdfView::ZoomMode::Custom);
	m_view->setZoomFactor(qMin(8.0, m_view->zoomFactor() * 1.25));
}

void PdfViewerWindow::zoomOut()
{
	m_view->setZoomMode(QPdfView::ZoomMode::Custom);
	m_view->setZoomFactor(qMax(0.1, m_view->zoomFactor() / 1.25));
}

void PdfViewerWindow::fitWidth()
{
	m_view->setZoomMode(QPdfView::ZoomMode::FitToWidth);
}

void PdfViewerWindow::updatePageLabel()
{
	const int total = m_doc ? m_doc->pageCount() : 0;
	const int cur = (m_view && total > 0)
		? m_view->pageNavigator()->currentPage() + 1 : 0;
	m_page_label->setText(QStringLiteral(" %1 / %2 ").arg(cur).arg(total));
}
