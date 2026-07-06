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
#ifndef FOLIOREVISIONSDIALOG_H
#define FOLIOREVISIONSDIALOG_H

#include "../titleblockproperties.h"

#include <QDialog>

class Diagram;
class QComboBox;
class QDateEdit;
class QLineEdit;
class QTabWidget;
class QTableWidget;

/**
	@brief The FolioRevisionsDialog class
	Dialog to edit the revision history of the current folio's title
	block: revision index, document status, date of issue and the six
	rev1..rev6 rows (index / date / zone / description / by / approved)
	used by the company title block templates.
*/
class FolioRevisionsDialog : public QDialog
{
	Q_OBJECT

	public:
	static void edit(Diagram *diagram, QWidget *parent = nullptr);

	protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

	private:
	FolioRevisionsDialog(Diagram *diagram, QWidget *parent = nullptr);
	TitleBlockProperties editedProperties() const;
	void applyToAllFolios();
	void deleteSelectedRevisions();

	static const int ROW_COUNT = 6;

	Diagram *m_diagram;
	TitleBlockProperties m_original;
	QTabWidget *m_tabs;
	QLineEdit *m_indexrev;
	QComboBox *m_doc_status;
	QDateEdit *m_issue_date;
	QTableWidget *m_table;
	QDateEdit *m_cur_rev_date;
	QLineEdit *m_cur_rev_zone;
	QLineEdit *m_cur_rev_desc;
	QLineEdit *m_cur_rev_by;
	QLineEdit *m_cur_rev_appd;
	/// onglet « tous les folios »
	QLineEdit *m_all_indexrev;
	QComboBox *m_all_doc_status;
	QDateEdit *m_all_issue_date;
	QDateEdit *m_all_rev_date;
	QLineEdit *m_all_rev_zone;
	QLineEdit *m_all_rev_desc;
	QLineEdit *m_all_rev_by;
	QLineEdit *m_all_rev_appd;
};

#endif // FOLIOREVISIONSDIALOG_H
