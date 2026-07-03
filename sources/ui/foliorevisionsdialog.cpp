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

#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QTableWidget>
#include <QVBoxLayout>

namespace
{
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
#ifdef Q_OS_MACOS
	setWindowFlags(Qt::Sheet);
#endif

	auto *form = new QFormLayout();
	m_indexrev = new QLineEdit(m_original.indexrev, this);
	form->addRow(tr("Indice de révision :"), m_indexrev);

	m_doc_status = new QLineEdit(
		m_original.context.value(QStringLiteral("doc-status")).toString(),
		this);
	form->addRow(tr("État du document :"), m_doc_status);

	m_issue_date = new QDateEdit(this);
	m_issue_date->setCalendarPopup(true);
	m_issue_date->setDate(m_original.date.isValid()
			      ? m_original.date : QDate::currentDate());
	form->addRow(tr("Date de publication :"), m_issue_date);

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
	m_table->setMinimumWidth(680);

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
	layout->addWidget(buttons);

	if (m_diagram->isReadOnly()) {
		m_indexrev->setReadOnly(true);
		m_doc_status->setReadOnly(true);
		m_issue_date->setReadOnly(true);
		m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	}
}

/**
	@return a copy of the original title block properties with the
	values entered in the dialog applied.
*/
TitleBlockProperties FolioRevisionsDialog::editedProperties() const
{
	TitleBlockProperties properties = m_original;

	properties.indexrev = m_indexrev->text();
	properties.date = m_issue_date->date();
	properties.useDate = TitleBlockProperties::UseDateValue;

	/* etat du document : ne pas creer la cle si elle n'existe pas
	 * et que le champ est vide (projets sans cartouche société) */
	const QString doc_status = m_doc_status->text();
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
