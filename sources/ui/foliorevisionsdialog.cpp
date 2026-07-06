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
#include "foliorevisionsdialog.h"

#include "../diagram.h"
#include "../qetproject.h"
#include "../undocommand/changetitleblockcommand.h"

#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace
{
	/* editeur calendrier pour la colonne « date » du tableau */
	class RevisionDateDelegate : public QStyledItemDelegate
	{
		public:
		using QStyledItemDelegate::QStyledItemDelegate;

		QWidget *createEditor(QWidget *parent,
				      const QStyleOptionViewItem &,
				      const QModelIndex &index) const override
		{
			auto *editor = new QDateEdit(parent);
			editor->setCalendarPopup(true);
			const QDate current = QDate::fromString(
				index.data().toString(),
				QStringLiteral("yyyy/M/d"));
			editor->setDate(current.isValid()
					? current : QDate::currentDate());
			return editor;
		}

		void setModelData(QWidget *editor,
				  QAbstractItemModel *model,
				  const QModelIndex &index) const override
		{
			model->setData(index,
				static_cast<QDateEdit *>(editor)->date()
					.toString(QStringLiteral("yyyy/M/d")));
		}
	};

	// champs d'une ligne de revision, dans l'ordre des colonnes
	const char *REV_FIELDS[] = { "idx", "date", "zone", "desc", "by", "appd" };
	const int REV_FIELD_COUNT = 6;

	QString revKey(int row, int column)
	{
		return QStringLiteral("rev%1-%2")
			.arg(row + 1)
			.arg(QLatin1String(REV_FIELDS[column]));
	}
}

/**
	Open the dialog for \a diagram; on acceptance, push an undoable
	title block modification on the diagram's undo stack.
*/
void FolioRevisionsDialog::edit(Diagram *diagram, QWidget *parent)
{
	if (!diagram) return;

	FolioRevisionsDialog dialog(diagram, parent);
	if (dialog.exec() != QDialog::Accepted || diagram->isReadOnly()) {
		return;
	}

	//onglet « tous les folios » actif : application groupee
	if (dialog.m_tabs->currentIndex() == 1) {
		dialog.applyToAllFolios();
		return;
	}

	TitleBlockProperties new_properties = dialog.editedProperties();
	if (new_properties != dialog.m_original) {
		diagram->undoStack().push(new ChangeTitleBlockCommand(
			diagram, dialog.m_original, new_properties));
	}
}

FolioRevisionsDialog::FolioRevisionsDialog(Diagram *diagram, QWidget *parent) :
	QDialog(parent),
	m_diagram(diagram),
	m_original(diagram->border_and_titleblock.exportTitleBlock())
{
	setWindowTitle(tr("Révisions du folio", "window title"));

	auto *form = new QFormLayout();
	m_indexrev = new QLineEdit(m_original.indexrev, this);
	form->addRow(tr("Indice de révision :"), m_indexrev);

	m_doc_status = new QComboBox(this);
	m_doc_status->setEditable(true);
	m_doc_status->addItems({
		QStringLiteral("草案 Draft"),
		QStringLiteral("審核中 Under review"),
		QStringLiteral("正式發行 Released"),
		QStringLiteral("作廢 Obsolete"),
	});
	m_doc_status->setEditText(
		m_original.context.value(QStringLiteral("doc-status")).toString());
	form->addRow(tr("État du document :"), m_doc_status);

	m_issue_date = new QDateEdit(this);
	m_issue_date->setCalendarPopup(true);
	/* date minimale = valeur « vide » : affichee blanche et exportee
	 * comme date invalide (champ efface du cartouche) */
	m_issue_date->setMinimumDate(QDate(1900, 1, 1));
	m_issue_date->setSpecialValueText(QStringLiteral(" "));
	m_issue_date->setDate(m_original.date.isValid()
			      ? m_original.date
			      : m_issue_date->minimumDate());
	auto *issue_today = new QPushButton(tr("Aujourd'hui"), this);
	auto *issue_clear = new QPushButton(tr("Effacer"), this);
	connect(issue_today, &QPushButton::clicked, this, [this]() {
		m_issue_date->setDate(QDate::currentDate());
	});
	connect(issue_clear, &QPushButton::clicked, this, [this]() {
		m_issue_date->setDate(m_issue_date->minimumDate());
	});
	auto *issue_row = new QHBoxLayout();
	issue_row->addWidget(m_issue_date, 1);
	issue_row->addWidget(issue_today);
	issue_row->addWidget(issue_clear);
	form->addRow(tr("Date de publication :"), issue_row);

	m_table = new QTableWidget(ROW_COUNT, REV_FIELD_COUNT, this);
	m_table->setHorizontalHeaderLabels({
		tr("Indice"), tr("Date"), tr("Zone"),
		tr("Description de la révision"), tr("Par"), tr("Approuvé")});
	m_table->horizontalHeader()->setSectionResizeMode(
		3, QHeaderView::Stretch);
	m_table->verticalHeader()->setVisible(false);
	for (int row = 0 ; row < ROW_COUNT ; ++row) {
		for (int column = 0 ; column < REV_FIELD_COUNT ; ++column) {
			auto *item = new QTableWidgetItem(
				m_original.context.value(revKey(row, column))
					.toString());
			m_table->setItem(row, column, item);
		}
	}
	m_table->setItemDelegateForColumn(1, new RevisionDateDelegate(m_table));
	/* clic sur cellule selectionnee / double-clic / touche : le mode
	 * « toujours editer » ecrivait une date au simple passage ; la
	 * touche Suppr/Retour arriere vide les cellules selectionnees */
	m_table->setEditTriggers(QAbstractItemView::SelectedClicked
				 | QAbstractItemView::DoubleClicked
				 | QAbstractItemView::EditKeyPressed
				 | QAbstractItemView::AnyKeyPressed);
	m_table->installEventFilter(this);
	m_table->setMinimumWidth(680);

	auto *table_today = new QPushButton(
		tr("Date du jour sur la ligne sélectionnée"), this);
	connect(table_today, &QPushButton::clicked, this, [this]() {
		QSet<int> rows;
		const auto items = m_table->selectedItems();
		for (QTableWidgetItem *item : items) {
			rows.insert(item->row());
		}
		if (rows.isEmpty() && m_table->currentRow() >= 0) {
			rows.insert(m_table->currentRow());
		}
		const QString today = QDate::currentDate().toString(
			QStringLiteral("yyyy/M/d"));
		for (int row : rows) {
			m_table->item(row, 1)->setText(today);
		}
	});
	auto *table_button_row = new QHBoxLayout();
	table_button_row->addWidget(table_today);
	table_button_row->addStretch();

	auto *buttons = new QDialogButtonBox(
		m_diagram->isReadOnly()
			? QDialogButtonBox::Ok
			: QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
		this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	//page « ce folio »
	auto *current_page = new QWidget(this);
	auto *current_layout = new QVBoxLayout(current_page);
	current_layout->addLayout(form);
	current_layout->addWidget(m_table);
	current_layout->addLayout(table_button_row);

	//page « tous les folios » : reglage groupe
	auto *all_page = new QWidget(this);
	auto *all_form = new QFormLayout();
	auto *hint = new QLabel(
		tr("Laisser un champ vide pour ne pas le modifier."),
		all_page);
	hint->setWordWrap(true);

	m_all_indexrev = new QLineEdit(all_page);
	all_form->addRow(tr("Indice de révision :"), m_all_indexrev);

	m_all_doc_status = new QComboBox(all_page);
	m_all_doc_status->setEditable(true);
	m_all_doc_status->addItems({
		QString(),
		QStringLiteral("草案 Draft"),
		QStringLiteral("審核中 Under review"),
		QStringLiteral("正式發行 Released"),
		QStringLiteral("作廢 Obsolete"),
	});
	m_all_doc_status->setEditText(QString());
	all_form->addRow(tr("État du document :"), m_all_doc_status);

	m_all_issue_date = new QDateEdit(all_page);
	m_all_issue_date->setCalendarPopup(true);
	m_all_issue_date->setMinimumDate(QDate(1900, 1, 1));
	m_all_issue_date->setSpecialValueText(QStringLiteral(" "));
	m_all_issue_date->setDate(m_all_issue_date->minimumDate());
	auto *all_today = new QPushButton(tr("Aujourd'hui"), all_page);
	auto *all_clear = new QPushButton(tr("Effacer"), all_page);
	connect(all_today, &QPushButton::clicked, this, [this]() {
		m_all_issue_date->setDate(QDate::currentDate());
	});
	connect(all_clear, &QPushButton::clicked, this, [this]() {
		m_all_issue_date->setDate(m_all_issue_date->minimumDate());
	});
	auto *all_date_row = new QHBoxLayout();
	all_date_row->addWidget(m_all_issue_date, 1);
	all_date_row->addWidget(all_today);
	all_date_row->addWidget(all_clear);
	all_form->addRow(tr("Date de publication :"), all_date_row);

	auto *rev_group = new QGroupBox(
		tr("Ajouter une ligne de révision (première ligne vide de"
		   " chaque folio)"),
		all_page);
	auto *rev_form = new QFormLayout(rev_group);
	m_all_rev_idx = new QLineEdit(rev_group);
	rev_form->addRow(tr("Indice"), m_all_rev_idx);
	m_all_rev_date = new QDateEdit(rev_group);
	m_all_rev_date->setCalendarPopup(true);
	m_all_rev_date->setMinimumDate(QDate(1900, 1, 1));
	m_all_rev_date->setSpecialValueText(QStringLiteral(" "));
	m_all_rev_date->setDate(QDate::currentDate());
	rev_form->addRow(tr("Date"), m_all_rev_date);
	m_all_rev_zone = new QLineEdit(rev_group);
	rev_form->addRow(tr("Zone"), m_all_rev_zone);
	m_all_rev_desc = new QLineEdit(rev_group);
	rev_form->addRow(tr("Description de la révision"), m_all_rev_desc);
	m_all_rev_by = new QLineEdit(rev_group);
	rev_form->addRow(tr("Par"), m_all_rev_by);
	m_all_rev_appd = new QLineEdit(rev_group);
	rev_form->addRow(tr("Approuvé"), m_all_rev_appd);

	auto *all_layout = new QVBoxLayout(all_page);
	all_layout->addWidget(hint);
	all_layout->addLayout(all_form);
	all_layout->addWidget(rev_group);
	all_layout->addStretch();

	m_tabs = new QTabWidget(this);
	m_tabs->addTab(current_page, tr("Ce folio"));
	m_tabs->addTab(all_page, tr("Tous les folios"));
	if (m_diagram->isReadOnly()) {
		all_page->setEnabled(false);
	}

	auto *layout = new QVBoxLayout(this);
	layout->addWidget(m_tabs);
	layout->addWidget(buttons);

	if (m_diagram->isReadOnly()) {
		m_indexrev->setReadOnly(true);
		m_doc_status->setEnabled(false);
		m_issue_date->setReadOnly(true);
		issue_today->setEnabled(false);
		issue_clear->setEnabled(false);
		table_today->setEnabled(false);
		m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	}
}

/**
	Delete / Backspace clears the selected revision cells (this is the
	only way to empty a date cell, since the calendar editor always
	commits a valid date).
*/
bool FolioRevisionsDialog::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == m_table && event->type() == QEvent::KeyPress) {
		auto *key_event = static_cast<QKeyEvent *>(event);
		if (key_event->key() == Qt::Key_Delete
		    || key_event->key() == Qt::Key_Backspace) {
			const auto items = m_table->selectedItems();
			for (QTableWidgetItem *item : items) {
				item->setText(QString());
			}
			return true;
		}
	}
	return QDialog::eventFilter(watched, event);
}

/**
	Apply the batch tab to every folio of the project : revision index /
	document status / issue date (empty fields left untouched), and
	optionally one revision line written into the FIRST EMPTY rev slot of
	each folio -- never overwriting an existing revision entry.
*/
void FolioRevisionsDialog::applyToAllFolios()
{
	QETProject *project = m_diagram->project();
	if (!project) return;

	const QString batch_index = m_all_indexrev->text().trimmed();
	const QString batch_status = m_all_doc_status->currentText().trimmed();
	const QDate batch_date =
		(m_all_issue_date->date() == m_all_issue_date->minimumDate())
			? QDate()
			: m_all_issue_date->date();

	const QString rev_date_text =
		(m_all_rev_date->date() == m_all_rev_date->minimumDate())
			? QString()
			: m_all_rev_date->date().toString(
				  QStringLiteral("yyyy/M/d"));
	const QStringList rev_values {
		m_all_rev_idx->text().trimmed(),
		rev_date_text,
		m_all_rev_zone->text().trimmed(),
		m_all_rev_desc->text().trimmed(),
		m_all_rev_by->text().trimmed(),
		m_all_rev_appd->text().trimmed() };
	bool rev_wanted = false;
	for (const QString &value : rev_values) {
		if (!value.isEmpty()) rev_wanted = true;
	}

	int applied = 0;
	int full = 0;
	const QList<Diagram *> diagrams = project->diagrams();
	for (Diagram *diagram : diagrams)
	{
		TitleBlockProperties old_properties =
			diagram->border_and_titleblock.exportTitleBlock();
		TitleBlockProperties new_properties = old_properties;
		bool changed = false;

		if (!batch_index.isEmpty()) {
			new_properties.indexrev = batch_index;
			changed = true;
		}
		if (!batch_status.isEmpty()) {
			new_properties.context.addValue(
				QStringLiteral("doc-status"), batch_status);
			changed = true;
		}
		if (batch_date.isValid()) {
			new_properties.date = batch_date;
			new_properties.useDate =
				TitleBlockProperties::UseDateValue;
			changed = true;
		}

		if (rev_wanted)
		{
			//premiere ligne de revision entierement vide
			int free_row = -1;
			for (int row = 0 ; row < ROW_COUNT ; ++row) {
				bool empty = true;
				for (int column = 0 ;
				     column < REV_FIELD_COUNT ; ++column) {
					if (!new_properties.context
						     .value(revKey(row, column))
						     .toString().isEmpty()) {
						empty = false;
						break;
					}
				}
				if (empty) { free_row = row; break; }
			}
			if (free_row == -1) {
				++full;
			} else {
				for (int column = 0 ;
				     column < REV_FIELD_COUNT ; ++column) {
					new_properties.context.addValue(
						revKey(free_row, column),
						rev_values.at(column));
				}
				changed = true;
			}
		}

		if (changed && new_properties != old_properties) {
			diagram->undoStack().push(new ChangeTitleBlockCommand(
				diagram, old_properties, new_properties));
			++applied;
		}
	}

	QMessageBox::information(
		this->parentWidget() ? this->parentWidget() : nullptr,
		tr("Révisions du folio", "window title"),
		tr("Appliqué à %1 folios. %2 folios sans ligne de révision"
		   " libre (non modifiés).").arg(applied).arg(full));
}

/**
	@return a copy of the original title block properties with the
	values entered in the dialog applied.
*/
TitleBlockProperties FolioRevisionsDialog::editedProperties() const
{
	TitleBlockProperties properties = m_original;

	properties.indexrev = m_indexrev->text();
	const QDate issue_date = m_issue_date->date();
	properties.date = (issue_date == m_issue_date->minimumDate())
				  ? QDate()
				  : issue_date;
	properties.useDate = TitleBlockProperties::UseDateValue;

	/* etat du document : ne pas creer la cle si elle n'existe pas
	 * et que le champ est vide (projets sans cartouche société) */
	const QString doc_status = m_doc_status->currentText();
	if (!doc_status.isEmpty()
	    || properties.context.keys().contains(QStringLiteral("doc-status"))) {
		properties.context.addValue(
			QStringLiteral("doc-status"), doc_status);
	}

	/* lignes de revision : si au moins une cellule est renseignee (ou
	 * que les cles existent deja), ecrire les 36 cles, y compris les
	 * vides, pour que le cartouche affiche des blancs plutot que les
	 * variables %{revN-...} brutes */
	bool any_value = false;
	for (int row = 0 ; row < ROW_COUNT && !any_value ; ++row) {
		for (int column = 0 ; column < REV_FIELD_COUNT ; ++column) {
			if (!m_table->item(row, column)->text().isEmpty()) {
				any_value = true;
				break;
			}
		}
	}
	if (any_value
	    || properties.context.keys().contains(QStringLiteral("rev1-idx"))) {
		for (int row = 0 ; row < ROW_COUNT ; ++row) {
			for (int column = 0 ; column < REV_FIELD_COUNT ; ++column) {
				properties.context.addValue(
					revKey(row, column),
					m_table->item(row, column)->text());
			}
		}
	}

	return properties;
}
