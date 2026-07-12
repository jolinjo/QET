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

#include "../Pdm/pdmsettings.h"
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

	// 已登入 PDM:修訂索引(版本)、文件狀態、發行日期都由系統管理(版本三拆、
	// 核准發行時填),使用者不手動設定 → 隱藏這三列。
	if (!PdmSettings::username().isEmpty()) {
		form->setRowVisible(m_indexrev, false);
		form->setRowVisible(m_doc_status, false);
		form->setRowVisible(issue_row, false);
	}

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
	/* tableau en lecture seule : la saisie passe par le formulaire
	 * « ajouter une revision », la suppression par ligne entiere */
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->installEventFilter(this);
	m_table->setMinimumWidth(680);

	auto *delete_revision = new QPushButton(
		tr("Supprimer la révision sélectionnée"), this);
	connect(delete_revision, &QPushButton::clicked,
		this, &FolioRevisionsDialog::deleteSelectedRevisions);
	auto *table_button_row = new QHBoxLayout();
	table_button_row->addWidget(delete_revision);
	table_button_row->addStretch();

	/* formulaire d'ajout : l'indice reprend celui saisi plus haut, la
	 * ligne cible (premiere vide, defilement si plein) est choisie
	 * automatiquement a la validation */
	auto *cur_rev_group = new QGroupBox(tr("Ajouter une révision"), this);
	auto *cur_rev_form = new QFormLayout(cur_rev_group);
	// 版次(修訂索引):系統自動由下方表格現有版次累加,使用者不可設定。
	auto *cur_rev_idx = new QLineEdit(cur_rev_group);
	cur_rev_idx->setReadOnly(true);
	cur_rev_form->addRow(tr("Indice"), cur_rev_idx);
	m_cur_rev_date = new QDateEdit(cur_rev_group);
	m_cur_rev_date->setCalendarPopup(true);
	m_cur_rev_date->setMinimumDate(QDate(1900, 1, 1));
	m_cur_rev_date->setSpecialValueText(QStringLiteral(" "));
	m_cur_rev_date->setDate(QDate::currentDate());
	cur_rev_form->addRow(tr("Date"), m_cur_rev_date);
	m_cur_rev_zone = new QLineEdit(cur_rev_group);
	cur_rev_form->addRow(tr("Zone"), m_cur_rev_zone);
	m_cur_rev_desc = new QLineEdit(cur_rev_group);
	cur_rev_form->addRow(tr("Description de la révision"), m_cur_rev_desc);
	m_cur_rev_by = new QLineEdit(cur_rev_group);
	// 修改者:已登入 PDM 則自動填帳號且不可改;未登入(純本機用)才可手填。
	{
		const QString pdm_user = PdmSettings::username();
		if (!pdm_user.isEmpty()) {
			m_cur_rev_by->setText(pdm_user);
			m_cur_rev_by->setReadOnly(true);
		}
	}
	cur_rev_form->addRow(tr("Par"), m_cur_rev_by);
	// 核准者由系統於「核准發行」時自動填,使用者不能設定 → 不在表單顯示。
	// 保留物件供 editedProperties 讀取(永遠空值)。
	m_cur_rev_appd = new QLineEdit(cur_rev_group);
	m_cur_rev_appd->hide();

	// 由下方表格現有版次自動推算「下一個版次」:全數字→最大值+1;
	// 單一字母→下一字母;皆無→從 1 起。使用者不需(也不能)手動設定。
	auto computeNextIdx = [this]() -> QString {
		int max_num = -1; bool has_num = false;
		QChar max_alpha; bool has_alpha = false;
		for (int row = 0 ; row < ROW_COUNT ; ++row) {
			const QString v = m_table->item(row, 0)->text().trimmed();
			if (v.isEmpty()) continue;
			bool ok = false;
			const int n = v.toInt(&ok);
			if (ok) { has_num = true; if (n > max_num) max_num = n; }
			else if (v.size() == 1 && v.at(0).isLetter()) {
				has_alpha = true;
				const QChar c = v.at(0).toUpper();
				if (max_alpha.isNull() || c > max_alpha) max_alpha = c;
			}
		}
		if (has_num) return QString::number(max_num + 1);
		if (has_alpha && max_alpha < QLatin1Char('Z'))
			return QString(QChar(max_alpha.unicode() + 1));
		if (has_alpha) return QString(max_alpha);   // 已到 Z:不再遞增
		return QStringLiteral("1");
	};
	cur_rev_idx->setText(computeNextIdx());

	// 「加入修訂記錄」:把目前表單的一筆寫進下方表格,清空表單以便再加下一筆
	//(可一次加多筆);按「確定」時 editedProperties 會把表格所有列存回。
	auto *add_rev_button = new QPushButton(tr("加入修訂記錄"), cur_rev_group);
	add_rev_button->setEnabled(!m_diagram->isReadOnly());
	cur_rev_form->addRow(QString(), add_rev_button);
	connect(add_rev_button, &QPushButton::clicked, this,
		[this, cur_rev_idx, computeNextIdx]() {
		const QString desc = m_cur_rev_desc->text().trimmed();
		if (desc.isEmpty()) {
			QMessageBox::warning(this, tr("加入修訂記錄"),
				tr("請先填寫「修改內容」。"));
			return;
		}
		const QString date =
			(m_cur_rev_date->date() == m_cur_rev_date->minimumDate())
				? QString()
				: m_cur_rev_date->date().toString(
					  QStringLiteral("yyyy/M/d"));
		const QStringList vals {
			cur_rev_idx->text().trimmed(), date,
			m_cur_rev_zone->text().trimmed(), desc,
			m_cur_rev_by->text().trimmed(),
			m_cur_rev_appd->text().trimmed() };
		// 找第一列全空的,滿了就滾動(移除最舊、寫最後一列)
		int free_row = -1;
		for (int row = 0 ; row < ROW_COUNT ; ++row) {
			bool empty = true;
			for (int c = 0 ; c < REV_FIELD_COUNT ; ++c)
				if (!m_table->item(row, c)->text().isEmpty()) {
					empty = false; break;
				}
			if (empty) { free_row = row; break; }
		}
		if (free_row == -1) {
			for (int row = 0 ; row < ROW_COUNT - 1 ; ++row)
				for (int c = 0 ; c < REV_FIELD_COUNT ; ++c)
					m_table->item(row, c)->setText(
						m_table->item(row + 1, c)->text());
			free_row = ROW_COUNT - 1;
		}
		for (int c = 0 ; c < REV_FIELD_COUNT ; ++c)
			m_table->item(free_row, c)->setText(vals.at(c));
		// 清空表單以便再加下一筆(修改者保留;日期回今天);版次重新推算
		m_cur_rev_desc->clear();
		m_cur_rev_zone->clear();
		m_cur_rev_date->setDate(QDate::currentDate());
		cur_rev_idx->setText(computeNextIdx());
		m_cur_rev_desc->setFocus();
	});

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
	//saisies en haut, tableau (affichage) en bas
	current_layout->addLayout(form);
	current_layout->addWidget(cur_rev_group);
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
	//la colonne « indice » de la ligne reprend l'indice de revision
	//saisi plus haut : pas de champ dedie
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
	{
		const QString pdm_user = PdmSettings::username();
		if (!pdm_user.isEmpty()) {
			m_all_rev_by->setText(pdm_user);
			m_all_rev_by->setReadOnly(true);
		}
	}
	rev_form->addRow(tr("Par"), m_all_rev_by);
	// 核准者由系統於核准發行時自動填,不在表單顯示(保留物件、空值)
	m_all_rev_appd = new QLineEdit(rev_group);
	m_all_rev_appd->hide();

	auto *all_layout = new QVBoxLayout(all_page);
	all_layout->addWidget(hint);
	all_layout->addLayout(all_form);
	all_layout->addWidget(rev_group);
	all_layout->addStretch();

	m_tabs = new QTabWidget(this);
	m_tabs->addTab(current_page, tr("Ce folio"));
	// 登入 PDM 時不顯示「全部頁面」批次頁籤(修訂逐頁記錄,避免整批套用)
	if (PdmSettings::username().isEmpty())
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
		delete_revision->setEnabled(false);
		cur_rev_group->setEnabled(false);
	}
}

/**
	Delete / Backspace removes the selected revision line(s), like the
	dedicated button.
*/
bool FolioRevisionsDialog::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == m_table && event->type() == QEvent::KeyPress) {
		auto *key_event = static_cast<QKeyEvent *>(event);
		if (key_event->key() == Qt::Key_Delete
		    || key_event->key() == Qt::Key_Backspace) {
			if (!m_diagram->isReadOnly()) {
				deleteSelectedRevisions();
			}
			return true;
		}
	}
	return QDialog::eventFilter(watched, event);
}

/**
	Remove the selected revision line(s) and compact the remaining ones
	upward, so the history stays gapless.
*/
void FolioRevisionsDialog::deleteSelectedRevisions()
{
	QSet<int> rows;
	const auto items = m_table->selectedItems();
	for (QTableWidgetItem *item : items) {
		rows.insert(item->row());
	}
	if (rows.isEmpty() && m_table->currentRow() >= 0) {
		rows.insert(m_table->currentRow());
	}
	if (rows.isEmpty()) return;

	QList<QStringList> kept;
	for (int row = 0 ; row < ROW_COUNT ; ++row) {
		if (rows.contains(row)) continue;
		QStringList values;
		bool empty = true;
		for (int column = 0 ; column < REV_FIELD_COUNT ; ++column) {
			const QString value = m_table->item(row, column)->text();
			if (!value.isEmpty()) empty = false;
			values << value;
		}
		if (!empty) kept << values;
	}
	for (int row = 0 ; row < ROW_COUNT ; ++row) {
		for (int column = 0 ; column < REV_FIELD_COUNT ; ++column) {
			m_table->item(row, column)->setText(
				row < kept.count() ? kept.at(row).at(column)
						   : QString());
		}
	}
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
		batch_index,
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
				/* six lignes occupees : faire defiler --
				 * la plus ancienne (ligne 1) disparait,
				 * la nouvelle s'ecrit en ligne 6 */
				for (int row = 0 ; row < ROW_COUNT - 1 ;
				     ++row) {
					for (int column = 0 ;
					     column < REV_FIELD_COUNT ;
					     ++column) {
						new_properties.context.addValue(
							revKey(row, column),
							new_properties.context
								.value(revKey(row + 1,
									      column))
								.toString());
					}
				}
				free_row = ROW_COUNT - 1;
				++full;
			}
			for (int column = 0 ;
			     column < REV_FIELD_COUNT ; ++column) {
				new_properties.context.addValue(
					revKey(free_row, column),
					rev_values.at(column));
			}
			changed = true;
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
		tr("Appliqué à %1 folios (%2 avec défilement : la révision"
		   " la plus ancienne a été retirée).").arg(applied).arg(full));
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
	/* 只保存表格內容:新增修訂一律透過「加入修訂記錄」按鈕寫入表格,
	 * 按「確定」不再自行補一列(使用者要求:確定後直接關閉即可)。 */
	if (any_value
	    || properties.context.keys().contains(QStringLiteral("rev1-idx"))) {
		//contenu du tableau (lignes existantes, deja compactees)
		QList<QStringList> lines;
		for (int row = 0 ; row < ROW_COUNT ; ++row) {
			QStringList values;
			bool empty = true;
			for (int column = 0 ; column < REV_FIELD_COUNT ;
			     ++column) {
				const QString value =
					m_table->item(row, column)->text();
				if (!value.isEmpty()) empty = false;
				values << value;
			}
			if (!empty) lines << values;
		}
		for (int row = 0 ; row < ROW_COUNT ; ++row) {
			for (int column = 0 ; column < REV_FIELD_COUNT ;
			     ++column) {
				properties.context.addValue(
					revKey(row, column),
					row < lines.count()
						? lines.at(row).at(column)
						: QString());
			}
		}
	}

	return properties;
}
