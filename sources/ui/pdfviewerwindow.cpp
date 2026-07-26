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

#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPdfDocument>
#include <QPdfPageNavigator>
#include <QPdfView>
#include <QToolBar>

PdfViewerWindow::PdfViewerWindow(QWidget *parent) :
	QMainWindow(parent)
{
	setWindowTitle(tr("PDF 檢視"));
	resize(900, 1000);

	m_doc = new QPdfDocument(this);
	m_view = new QPdfView(this);
	m_view->setDocument(m_doc);
	m_view->setPageMode(QPdfView::PageMode::MultiPage);
	m_view->setZoomMode(QPdfView::ZoomMode::FitToWidth);
	setCentralWidget(m_view);

	auto *tb = addToolBar(tr("PDF"));
	tb->setMovable(false);
	tb->addAction(tr("開啟…"), this, &PdfViewerWindow::openFileDialog);
	tb->addSeparator();
	tb->addAction(tr("上一頁"), this, &PdfViewerWindow::prevPage);
	tb->addAction(tr("下一頁"), this, &PdfViewerWindow::nextPage);
	m_page_label = new QLabel(QStringLiteral("  –  "), this);
	tb->addWidget(m_page_label);
	tb->addSeparator();
	tb->addAction(tr("放大"), this, &PdfViewerWindow::zoomIn);
	tb->addAction(tr("縮小"), this, &PdfViewerWindow::zoomOut);
	tb->addAction(tr("適合寬度"), this, &PdfViewerWindow::fitWidth);

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
	return true;
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
