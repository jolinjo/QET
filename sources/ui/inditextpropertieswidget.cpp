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
#include "inditextpropertieswidget.h"

#include <QColorDialog>
#include <QTextCursor>
#include <QTextDocument>

#include "../QPropertyUndoCommand/qpropertyundocommand.h"
#include "../diagram.h"
#include "../diagramcommands.h"
#include "../qetgraphicsitem/independenttextitem.h"
#include "../ui_inditextpropertieswidget.h"

#include <QLineEdit>
#include <QtGlobal>

namespace
{
	/* application d'un format de caracteres a tout le texte : passe par
	 * le html complet de l'item pour rester annulable */
	class ChangeTextHtmlCommand : public QUndoCommand
	{
		public:
		ChangeTextHtmlCommand(IndependentTextItem *item,
				      const QString &before,
				      const QString &after,
				      const QString &text) :
			m_item(item), m_before(before), m_after(after)
		{ setText(text); }

		void redo() override
		{ if (m_item) m_item->setHtml(m_after); }
		void undo() override
		{ if (m_item) m_item->setHtml(m_before); }

		private:
		QPointer<IndependentTextItem> m_item;
		QString m_before;
		QString m_after;
	};
}

/**
	@brief IndiTextPropertiesWidget::IndiTextPropertiesWidget
	@param text : the text to edit
	@param parent : the parent widget of this widget
*/
IndiTextPropertiesWidget::IndiTextPropertiesWidget(IndependentTextItem *text, QWidget *parent) :
	PropertiesEditorWidget(parent),
	ui(new Ui::IndiTextPropertiesWidget)
{
	ui->setupUi(this);
	{
		QFont bold_font = ui->m_bold_pb->font();
		bold_font.setBold(true);
		ui->m_bold_pb->setFont(bold_font);
		QFont underline_font = ui->m_underline_pb->font();
		underline_font.setUnderline(true);
		ui->m_underline_pb->setFont(underline_font);
	}
	if (text) {
		setText(text);
	}
}

/**
	@brief IndiTextPropertiesWidget::IndiTextPropertiesWidget
	@param text_list : a list of texts to edit
	@param parent : the parent widget of this widget
*/
IndiTextPropertiesWidget::IndiTextPropertiesWidget(
		QList<IndependentTextItem *> text_list,
		QWidget *parent) :
	PropertiesEditorWidget (parent),
	ui(new Ui::IndiTextPropertiesWidget)
{
	ui->setupUi(this);
	setText(text_list);
}

/**
	@brief IndiTextPropertiesWidget::~IndiTextPropertiesWidget
*/
IndiTextPropertiesWidget::~IndiTextPropertiesWidget()
{
	delete  ui;
}

/**
	@brief IndiTextPropertiesWidget::setText
	@param text : set text as edited text
*/
void IndiTextPropertiesWidget::setText(IndependentTextItem *text)
{
	if (m_text) {
		for (QMetaObject::Connection c : m_connect_list) {
			disconnect(c);
		}
	}
	
	m_text = text;
	m_connect_list.clear();
	m_connect_list << connect(m_text.data(), &IndependentTextItem::xChanged, this, &IndiTextPropertiesWidget::updateUi);
	m_connect_list << connect(m_text.data(), &IndependentTextItem::yChanged, this, &IndiTextPropertiesWidget::updateUi);
	m_connect_list << connect(m_text.data(), &IndependentTextItem::rotationChanged, this, &IndiTextPropertiesWidget::updateUi);
	m_connect_list << connect(m_text.data(), &IndependentTextItem::fontChanged, this, &IndiTextPropertiesWidget::updateUi);
	m_connect_list << connect(m_text.data(), &IndependentTextItem::textEdited, this, &IndiTextPropertiesWidget::updateUi);

	updateUi();
}

void IndiTextPropertiesWidget::setText(QList<IndependentTextItem *> text_list)
{
	for (QMetaObject::Connection c : m_connect_list) {
		disconnect(c);
	}
	m_connect_list.clear();
	m_text_list.clear();
	m_text = nullptr;
	
	if (text_list.size() == 0) {
		updateUi();
	}
	else if (text_list.size() == 1) 
	{
		setText(text_list.first());
		m_text_list.clear();
	}
	else
	{
		for (IndependentTextItem *iti : text_list) {
			m_text_list.append(QPointer<IndependentTextItem>(iti));
		}
		updateUi();
	}
}

/**
	@brief IndiTextPropertiesWidget::apply
	Apply the current edition through a QUndoCommand pushed
	to the undo stack of text's diagram.
*/
void IndiTextPropertiesWidget::apply()
{
	Diagram *d = nullptr;
	
	if (m_text && m_text->diagram()) {
		d = m_text->diagram();
	} else if (!m_text_list.isEmpty()) {
		for (QPointer<IndependentTextItem> piti : m_text_list) {
			if (piti->diagram()) {
				d = piti->diagram();
				break;
			}
		}
	}
	
	if (d)
	{
		QUndoCommand *undo = associatedUndo();
		if (undo) {
			d->undoStack().push(undo);
		}
	}
}

/**
	@brief IndiTextPropertiesWidget::setLiveEdit
	@param live_edit
	@return 
*/
bool IndiTextPropertiesWidget::setLiveEdit(bool live_edit)
{
	if (m_live_edit == live_edit) {
		return true;
	}
	m_live_edit = live_edit;
	
	if (m_live_edit) {
		setUpEditConnection();
	}
	else {
		for (QMetaObject::Connection c : m_edit_connection) {
			disconnect(c);
		}
		m_edit_connection.clear();
	}
	return true;
}

/**
	@brief IndiTextPropertiesWidget::associatedUndo
	@return 
*/
QUndoCommand *IndiTextPropertiesWidget::associatedUndo() const
{
	if (m_live_edit)
	{
		QPropertyUndoCommand *undo = nullptr;
			//One text is edited
		if (m_text_list.isEmpty())
		{
			if(ui->m_x_sb->value() != m_text->pos().x()) {
				undo = new QPropertyUndoCommand(m_text.data(), "x", QVariant(m_text->pos().x()), QVariant(ui->m_x_sb->value()));
				undo->setAnimated(true, false);
				undo->setText(tr("Déplacer un champ texte"));
			}
			if(ui->m_y_sb->value() != m_text->pos().y()) {
				undo = new QPropertyUndoCommand(m_text.data(), "y", QVariant(m_text->pos().y()), QVariant(ui->m_y_sb->value()));
				undo->setAnimated(true, false);
				undo->setText(tr("Déplacer un champ texte"));
			}
			if(ui->m_angle_sb->value() != m_text->rotation()) {
				undo = new QPropertyUndoCommand(m_text.data(), "rotation", QVariant(m_text->rotation()), QVariant(ui->m_angle_sb->value()));
				undo->setAnimated(true, false);
				undo->setText(tr("Pivoter un champ texte"));
			}
			if (ui->m_line_edit->text() != m_text->toPlainText()) {
				undo = new QPropertyUndoCommand(m_text.data(), "plainText", m_text->toPlainText(), ui->m_line_edit->text());
				undo->setText(tr("Modifier un champ texte"));
			}
			if (ui->m_size_sb->value() != m_text->font().pointSize()) {
				QFont font = m_text->font();
				font.setPointSize(ui->m_size_sb->value());
				undo = new QPropertyUndoCommand(m_text.data(), "font", m_text->font(), font);
				undo->setText(tr("Modifier la taille d'un champ texte"));
			}
			if (m_font_is_selected &&
				m_selected_font != m_text->font()) {
				undo = new QPropertyUndoCommand(m_text.data(), "font", m_text->font(), m_selected_font);
				undo->setText(tr("Modifier la police d'un champ texte"));
			}
			
			return undo;
		}
		else //several text are edited, only size and rotation is available for edition
		{
			QUndoCommand *parent_undo = nullptr;
			bool size_equal = true;
			bool angle_equal = true;
			bool font_equal = true;
			qreal rotation_ = m_text_list.first()->rotation();
			int size_ = m_text_list.first()->font().pointSize();
			QFont font_ = m_text_list.first()->font();
			for (QPointer<IndependentTextItem> piti : m_text_list)
			{
				if (piti->rotation() != rotation_) {
					angle_equal = false;
				}
				if (piti->font().pointSize() != size_) {
					size_equal = false;
				}
				if (piti->font() != font_) {
					font_equal = false;
				}
			}
				
			if ((angle_equal && (ui->m_angle_sb->value() != rotation_)) ||
				(!angle_equal && (ui->m_angle_sb->value() != ui->m_angle_sb->minimum())))
			{
				for (QPointer<IndependentTextItem> piti : m_text_list)
				{
					if (piti)
					{
						if (!parent_undo) {
							parent_undo = new QUndoCommand(tr("Pivoter plusieurs champs texte"));
						}
						QPropertyUndoCommand *qpuc = new QPropertyUndoCommand(piti.data(), "rotation", QVariant(piti->rotation()), QVariant(ui->m_angle_sb->value()), parent_undo);
						qpuc->setAnimated(true, false);
					}
				}
			}
			else if ((size_equal && (ui->m_size_sb->value() != size_)) ||
					 (!size_equal && (ui->m_size_sb->value() != ui->m_size_sb->minimum())))
			{
				for (QPointer<IndependentTextItem> piti : m_text_list)
				{
					if (piti)
					{
						if (!parent_undo) {
							parent_undo = new QUndoCommand(tr("Modifier la taille de plusieurs champs texte"));
						}
						QFont font = piti->font();
						font.setPointSize(ui->m_size_sb->value());
						new QPropertyUndoCommand(piti.data(), "font", QVariant(piti->font()), QVariant(font), parent_undo);
					}
				}
			}
			else if ((m_font_is_selected && !font_equal) ||
					 (m_font_is_selected && (font_equal && (m_selected_font != font_))))
			{
				for (QPointer<IndependentTextItem> piti : m_text_list)
				{
					if (piti)
					{
						if (!parent_undo) {
							parent_undo = new QUndoCommand(tr("Modifier la police de plusieurs champs texte"));
						}
						new QPropertyUndoCommand(piti.data(), "font", piti->font(), m_selected_font, parent_undo);
					}
				}
			}
			return parent_undo;
		}
	}
		//In mode not live edit, only one text can be edited
	else if (m_text_list.isEmpty())
	{
		QUndoCommand *undo = new QUndoCommand(tr("Modifier les propriétés d'un texte"));
		if(ui->m_x_sb->value() != m_text->pos().x()) {
			new QPropertyUndoCommand(m_text.data(), "x", QVariant(m_text->pos().x()), QVariant(ui->m_x_sb->value()), undo);
		}
		if(ui->m_y_sb->value() != m_text->pos().y()) {
			new QPropertyUndoCommand(m_text.data(), "y", QVariant(m_text->pos().y()), QVariant(ui->m_y_sb->value()), undo);
		}
		if(ui->m_angle_sb->value() != m_text->rotation()) {
			new QPropertyUndoCommand(m_text.data(), "rotation", QVariant(m_text->rotation()), QVariant(ui->m_angle_sb->value()), undo);
		}
		if (ui->m_line_edit->text() != m_text->toPlainText()) {
			new ChangeDiagramTextCommand(m_text.data(), m_text->toHtml(), ui->m_line_edit->text(), undo);
		}
		if (ui->m_size_sb->value() != m_text->font().pointSize())
		{
			QFont font = m_text->font();
			font.setPointSize(ui->m_size_sb->value());
			new QPropertyUndoCommand(m_text.data(), "font", m_text->font(), font, undo);
		}
		if (m_font_is_selected && m_selected_font != m_text->font()) {
			new QPropertyUndoCommand(m_text.data(), "font", m_text->font(), m_selected_font, undo);
		}
		
		if (undo->childCount()) {
			return undo;
		} else {
			return nullptr;
		}
	}
	else {
		return nullptr;
	}
}

/**
	@brief IndiTextPropertiesWidget::setUpEditConnection
	Disconnect the previous connection, and reconnect the connection between the editors widgets and void IndiTextPropertiesWidget::apply function
*/
void IndiTextPropertiesWidget::setUpEditConnection()
{
	for (QMetaObject::Connection c : m_edit_connection) {
		disconnect(c);
	}
	m_edit_connection.clear();
	
	if (m_text_list.isEmpty())
	{
		m_edit_connection << connect(ui->m_x_sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &IndiTextPropertiesWidget::apply);
		m_edit_connection << connect(ui->m_y_sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &IndiTextPropertiesWidget::apply);
		m_edit_connection << connect(ui->m_line_edit, &QLineEdit::textEdited, this, &IndiTextPropertiesWidget::apply);
	}
	m_edit_connection << connect(ui->m_angle_sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &IndiTextPropertiesWidget::apply);
	m_edit_connection << connect(ui->m_size_sb, QOverload<int>::of(&QSpinBox::valueChanged), [this]()
	{
		this->m_selected_font.setPointSize(ui->m_size_sb->value());
		this->apply();
	});
}

/**
	@brief IndiTextPropertiesWidget::updateUi
*/
void IndiTextPropertiesWidget::updateUi()
{
	if (!m_text && m_text_list.isEmpty()) {
		return;
	}

		//Disconnect every connections of editor widgets
		//to avoid an unwanted edition (QSpinBox emit valueChanged no matter if changer by user or by program)
	for (QMetaObject::Connection c : m_edit_connection) {
		disconnect(c);
	}
	m_edit_connection.clear();
	
	ui->m_x_sb->setEnabled(m_text_list.isEmpty() ? true : false);
	ui->m_y_sb->setEnabled(m_text_list.isEmpty() ? true : false);
	ui->m_line_edit->setEnabled(m_text_list.isEmpty() ? true : false);
	ui->m_advanced_editor_pb->setEnabled(m_text_list.isEmpty() ? true : false);
	
	if (m_text_list.isEmpty())
	{
		ui->m_x_sb->setValue(m_text->pos().x());
		ui->m_y_sb->setValue(m_text->pos().y());
		ui->m_line_edit->setText(m_text->toPlainText());
		ui->m_angle_sb->setValue(m_text->rotation());
		ui->m_size_sb->setValue(m_text->font().pointSize());
		
		ui->m_line_edit->setDisabled(m_text->isHtml() ? true : false);
		ui->m_size_sb->setDisabled(m_text->isHtml() ? true : false);
		ui->m_label->setVisible(m_text->isHtml() ? true : false);
		ui->m_break_html_pb->setVisible(m_text->isHtml() ? true : false);
		ui->m_font_pb->setDisabled(m_text->isHtml() ? true : false);
		ui->m_font_pb->setText(m_text->isHtml() ? tr("Police") : m_text->font().family());

		//etat des boutons de format (texte entier)
		ui->m_bold_pb->blockSignals(true);
		ui->m_underline_pb->blockSignals(true);
		ui->m_sup_pb->blockSignals(true);
		ui->m_sub_pb->blockSignals(true);
		ui->m_bold_pb->setChecked(m_text->font().bold());
		ui->m_underline_pb->setChecked(m_text->font().underline());
		QTextCursor cursor(m_text->document());
		cursor.select(QTextCursor::Document);
		const auto v_align = cursor.charFormat().verticalAlignment();
		ui->m_sup_pb->setChecked(
			v_align == QTextCharFormat::AlignSuperScript);
		ui->m_sub_pb->setChecked(
			v_align == QTextCharFormat::AlignSubScript);
		ui->m_bold_pb->blockSignals(false);
		ui->m_underline_pb->blockSignals(false);
		ui->m_sup_pb->blockSignals(false);
		ui->m_sub_pb->blockSignals(false);
	}
	else
	{
		bool size_equal = true;
		bool angle_equal = true;
		bool font_equal = true;
		qreal rotation_ = m_text_list.first()->rotation();
		int size_ = m_text_list.first()->font().pointSize();
		QFont font_ = m_text_list.first()->font();

		for (QPointer<IndependentTextItem> piti : m_text_list)
		{
			if (piti->rotation() != rotation_) {
				angle_equal = false;
			}
			if (piti->font().pointSize() != size_) {
				size_equal = false;
			}
			if (piti->font() != font_) {
				font_equal = false;
			}
		}
		ui->m_angle_sb->setValue(angle_equal ? rotation_ : 0);
		
		bool valid_ = true;
		for (QPointer<IndependentTextItem> piti : m_text_list) {
			if (piti->isHtml()) {
				valid_ = false;
			}
		}
		ui->m_font_pb->setEnabled(valid_);
		ui->m_font_pb->setText(font_equal ? font_.family() : tr("Police"));
		ui->m_size_sb->setEnabled(valid_);
		ui->m_size_sb->setValue(size_equal ? size_ : 0);
		ui->m_label->setVisible(false);
		ui->m_break_html_pb->setVisible(true);
	}

	
		//Set the connection now
	setUpEditConnection();
}

/**
	@return the texts currently edited by this widget
*/
QList<IndependentTextItem *> IndiTextPropertiesWidget::editedTexts() const
{
	QList<IndependentTextItem *> list;
	if (m_text) {
		list << m_text.data();
	}
	for (const QPointer<IndependentTextItem> &pointer : m_text_list) {
		if (pointer) list << pointer.data();
	}
	return list;
}

/**
	Merge \a format over the whole content of every edited text,
	as an undoable command.
*/
void IndiTextPropertiesWidget::applyCharFormatToAll(
		const QTextCharFormat &format,
		const QString &undo_text)
{
	const QList<IndependentTextItem *> texts = editedTexts();
	for (IndependentTextItem *item : texts) {
		const QString before = item->toHtml();
		QTextCursor cursor(item->document());
		cursor.select(QTextCursor::Document);
		cursor.mergeCharFormat(format);
		const QString after = item->toHtml();
		if (before == after) continue;
		if (item->diagram()) {
			item->diagram()->undoStack().push(
				new ChangeTextHtmlCommand(item, before, after,
							  undo_text));
		}
	}
}

void IndiTextPropertiesWidget::on_m_color_pb_clicked()
{
	const QList<IndependentTextItem *> texts = editedTexts();
	if (texts.isEmpty()) return;

	const QColor color = QColorDialog::getColor(
		texts.first()->color(), this,
		tr("Couleur du texte"));
	if (!color.isValid()) return;

	for (IndependentTextItem *item : texts) {
		if (item->isHtml()) {
			QTextCharFormat format;
			format.setForeground(color);
			QTextCursor cursor(item->document());
			cursor.select(QTextCursor::Document);
			const QString before = item->toHtml();
			cursor.mergeCharFormat(format);
			const QString after = item->toHtml();
			if (before != after && item->diagram()) {
				item->diagram()->undoStack().push(
					new ChangeTextHtmlCommand(
						item, before, after,
						tr("Modifier la couleur d'un"
						   " champ texte")));
			}
		} else if (item->color() != color && item->diagram()) {
			item->diagram()->undoStack().push(
				new QPropertyUndoCommand(
					item, "color",
					QVariant(item->color()),
					QVariant(color)));
		}
	}
}

void IndiTextPropertiesWidget::on_m_bold_pb_clicked(bool checked)
{
	const QList<IndependentTextItem *> texts = editedTexts();
	for (IndependentTextItem *item : texts) {
		if (item->isHtml()) {
			QTextCharFormat format;
			format.setFontWeight(checked ? QFont::Bold
						     : QFont::Normal);
			applyCharFormatToAll(format,
				tr("Modifier le format d'un champ texte"));
			return;
		}
	}
	for (IndependentTextItem *item : texts) {
		QFont font = item->font();
		if (font.bold() == checked) continue;
		font.setBold(checked);
		if (item->diagram()) {
			auto *undo = new QPropertyUndoCommand(
				item, "font",
				QVariant(item->font()), QVariant(font));
			undo->setText(
				tr("Modifier le format d'un champ texte"));
			item->diagram()->undoStack().push(undo);
		}
	}
}

void IndiTextPropertiesWidget::on_m_underline_pb_clicked(bool checked)
{
	const QList<IndependentTextItem *> texts = editedTexts();
	for (IndependentTextItem *item : texts) {
		if (item->isHtml()) {
			QTextCharFormat format;
			format.setFontUnderline(checked);
			applyCharFormatToAll(format,
				tr("Modifier le format d'un champ texte"));
			return;
		}
	}
	for (IndependentTextItem *item : texts) {
		QFont font = item->font();
		if (font.underline() == checked) continue;
		font.setUnderline(checked);
		if (item->diagram()) {
			auto *undo = new QPropertyUndoCommand(
				item, "font",
				QVariant(item->font()), QVariant(font));
			undo->setText(
				tr("Modifier le format d'un champ texte"));
			item->diagram()->undoStack().push(undo);
		}
	}
}

void IndiTextPropertiesWidget::on_m_sup_pb_clicked(bool checked)
{
	if (checked) {
		ui->m_sub_pb->blockSignals(true);
		ui->m_sub_pb->setChecked(false);
		ui->m_sub_pb->blockSignals(false);
	}
	QTextCharFormat format;
	format.setVerticalAlignment(checked
		? QTextCharFormat::AlignSuperScript
		: QTextCharFormat::AlignNormal);
	applyCharFormatToAll(format,
		tr("Modifier le format d'un champ texte"));
}

void IndiTextPropertiesWidget::on_m_sub_pb_clicked(bool checked)
{
	if (checked) {
		ui->m_sup_pb->blockSignals(true);
		ui->m_sup_pb->setChecked(false);
		ui->m_sup_pb->blockSignals(false);
	}
	QTextCharFormat format;
	format.setVerticalAlignment(checked
		? QTextCharFormat::AlignSubScript
		: QTextCharFormat::AlignNormal);
	applyCharFormatToAll(format,
		tr("Modifier le format d'un champ texte"));
}

/**
	@brief IndiTextPropertiesWidget::on_m_advanced_editor_pb_clicked
*/
void IndiTextPropertiesWidget::on_m_advanced_editor_pb_clicked()
{
	if (m_text) {
		m_text->edit();
	}
}

void IndiTextPropertiesWidget::on_m_break_html_pb_clicked()
{
	if (m_text) {
		m_text->setPlainText(m_text->toPlainText());
	}
	for (QPointer<IndependentTextItem> piti : m_text_list) {
		piti->setPlainText(piti->toPlainText());
	}
	
	updateUi();
}

void IndiTextPropertiesWidget::on_m_font_pb_clicked()
{
	if (!m_text && m_text_list.isEmpty()) {
		return;
	}
	bool ok;
	QFont font = m_text ? m_text->font() : m_text_list.first()->font();
	m_selected_font = QFontDialog::getFont(&ok, font, this);
	if (ok) {
		m_font_is_selected = true;
		ui->m_font_pb->setText(font.family());
		ui->m_size_sb->setValue(font.pointSize());
		apply();
	} else {
		ui->m_font_pb->setText(tr("Police"));
		m_font_is_selected = false;
	}
}
