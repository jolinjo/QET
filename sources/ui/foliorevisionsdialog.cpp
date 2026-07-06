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
#include "../undocommand/changetitleblockcommand.h"

#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QStyledItemDelegate>
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

	auto *layout = new QVBoxLayout(this);
	layout->addLayout(form);
	layout->addWidget(m_table);
	layout->addLayout(table_button_row);
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
