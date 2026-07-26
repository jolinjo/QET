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
#ifndef PDFVIEWERWINDOW_H
#define PDFVIEWERWINDOW_H

#include <QMainWindow>

class QPdfDocument;
class QPdfView;
class QLabel;

/**
	@brief 簡易 PDF 檢視視窗:開檔、翻頁、縮放。
	以 Qt PDF 模組(QPdfDocument + QPdfView)實作。
*/
class PdfViewerWindow : public QMainWindow
{
	Q_OBJECT

	public:
		explicit PdfViewerWindow(QWidget *parent = nullptr);
		~PdfViewerWindow() override;

		bool openFile(const QString &path);

	public slots:
		void openFileDialog();

	private:
		void prevPage();
		void nextPage();
		void zoomIn();
		void zoomOut();
		void fitWidth();
		void updatePageLabel();

		QPdfDocument *m_doc = nullptr;
		QPdfView *m_view = nullptr;
		QLabel *m_page_label = nullptr;
};

#endif // PDFVIEWERWINDOW_H
