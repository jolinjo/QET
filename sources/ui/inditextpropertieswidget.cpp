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
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>

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
				      const QString &text,
				      QUndoCommand *parent = nullptr) :
			QUndoCommand(parent),
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

	QString html_after_merge(IndependentTextItem *item,
				 const QTextCharFormat &format)
	{
		QScopedPointer<QTextDocument> clone(item->document()->clone());
		QTextCursor cursor(clone.data());
		cursor.select(QTextCursor::Document);
		cursor.mergeCharFormat(format);
		return clone->toHtml();
	}
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
	// 移除上標/下標:那是唯一會把文字轉成 HTML 的功能(HTML 會鎖住
	// 大小/字型/內容編輯)。拿掉後文字永遠純文字。
	ui->m_sup_pb->hide();
	ui->m_sub_pb->hide();
	// 「顏色」自訂調色盤按鈕改由下方「文字顏色」色票取代,隱藏之。
	ui->m_color_pb->hide();
	buildColorPalette();
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
	ui->m_sup_pb->hide();
	ui->m_sub_pb->hide();
	ui->m_color_pb->hide();
	buildColorPalette();
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
						if (piti->isHtml()) {
							QTextCharFormat format;
							format.setFontPointSize(
								ui->m_size_sb->value());
							new ChangeTextHtmlCommand(
								piti.data(),
								piti->toHtml(),
								html_after_merge(piti.data(), format),
								QString(), parent_undo);
						} else {
							QFont font = piti->font();
							font.setPointSize(ui->m_size_sb->value());
							new QPropertyUndoCommand(piti.data(), "font", QVariant(piti->font()), QVariant(font), parent_undo);
						}
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
						if (piti->isHtml()) {
							QTextCharFormat format;
							format.setFont(m_selected_font,
								QTextCharFormat::FontPropertiesAll);
							new ChangeTextHtmlCommand(
								piti.data(),
								piti->toHtml(),
								html_after_merge(piti.data(), format),
								QString(), parent_undo);
						} else {
							new QPropertyUndoCommand(piti.data(), "font", piti->font(), m_selected_font, parent_undo);
						}
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
			/* le champ de saisie n'est actif que pour un texte brut :
			 * memoriser l'etat "avant" en brut, pour que l'annulation
			 * ne transforme pas le texte en html */
			new ChangeDiagramTextCommand(m_text.data(),
				m_text->isHtml() ? m_text->toHtml() : m_text->toPlainText(),
				ui->m_line_edit->text(), undo);
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
		
		/* police et taille restent editables meme avec des textes en
		 * html : ceux-ci recoivent le format sur tout leur contenu */
		ui->m_font_pb->setEnabled(true);
		ui->m_font_pb->setText(font_equal ? font_.family() : tr("Police"));
		ui->m_size_sb->setEnabled(true);
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
		texts.first()->color(), this, tr("Couleur du texte"));
	if (!color.isValid()) return;
	applyTextForeground(color);
}

void IndiTextPropertiesWidget::applyTextForeground(const QColor &color)
{
	if (!color.isValid()) return;
	for (IndependentTextItem *item : editedTexts()) {
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

void IndiTextPropertiesWidget::applyTextBackground(const QColor &color)
{
	// 底色改用「項目層級圓角 badge 底」,而非字元格式 highlight。字元格式
	// 會把文字轉成 HTML,導致尺寸無法調整;badge 是項目屬性,不轉 HTML,
	// 尺寸照常可調。套用有效色時同時把文字色改成對比色(暗底白字/亮底
	// 深字)讓 badge 上的字清楚;整組合成單一復原。
	const QList<IndependentTextItem *> texts = editedTexts();
	for (IndependentTextItem *item : texts) {
		if (!item->diagram()) continue;
		if (item->badgeBackground() == color
		    && (!color.isValid())) continue;
		QUndoStack &st = item->diagram()->undoStack();
		st.beginMacro(color.isValid() ? tr("設定文字底色")
					      : tr("移除文字底色"));
		st.push(new QPropertyUndoCommand(item, "badgeBackground",
			QVariant(item->badgeBackground()), QVariant(color)));
		// 底色與外框互斥:設實心底色時清掉外框
		if (color.isValid() && item->badgeBorder().isValid())
			st.push(new QPropertyUndoCommand(item, "badgeBorder",
				QVariant(item->badgeBorder()), QVariant(QColor())));
		if (color.isValid() && !item->isHtml()) {
			// 用感知亮度(YIQ)決定對比色,而非 HSL lightness——
			// 飽和的紫/藍 lightness 會 ~0.5 卻其實很暗,需白字。
			const int yiq = (color.red() * 299 + color.green() * 587
					 + color.blue() * 114) / 1000;
			const QColor fg = yiq < 150
				? QColor(Qt::white) : QColor(0x1a, 0x1a, 0x1a);
			if (item->color() != fg)
				st.push(new QPropertyUndoCommand(item, "color",
					QVariant(item->color()), QVariant(fg)));
		}
		st.endMacro();
	}
}

void IndiTextPropertiesWidget::applyTextOutline(const QColor &color)
{
	// 外框:圓角彩色框、內部白底、文字黑字。與實心底色互斥。
	const QList<IndependentTextItem *> texts = editedTexts();
	for (IndependentTextItem *item : texts) {
		if (!item->diagram()) continue;
		QUndoStack &st = item->diagram()->undoStack();
		st.beginMacro(color.isValid() ? tr("設定文字外框")
					      : tr("移除文字外框"));
		st.push(new QPropertyUndoCommand(item, "badgeBorder",
			QVariant(item->badgeBorder()), QVariant(color)));
		if (color.isValid()) {
			if (item->badgeBackground().isValid())
				st.push(new QPropertyUndoCommand(item,
					"badgeBackground",
					QVariant(item->badgeBackground()),
					QVariant(QColor())));
			const QColor black(0x1a, 0x1a, 0x1a);
			if (!item->isHtml() && item->color() != black)
				st.push(new QPropertyUndoCommand(item, "color",
					QVariant(item->color()), QVariant(black)));
		}
		st.endMacro();
	}
}

void IndiTextPropertiesWidget::buildColorPalette()
{
	// 固定範本色(參考 ClickUp):文字色一組、底色(highlight)一組,
	// 直接點選套用,不必開調色盤,產出風格較一致。
	static const char *const TEXT_COLORS[] = {
		"#1a1a1a", "#e03e3e", "#d9730d", "#dfab01", "#0f7b6c",
		"#0b6e99", "#6940a5", "#ad1a72", "#787774" };
	// badge 底色(圓角標籤):一排飽和色、一排淡色(參考 ClickUp Badges)
	static const char *const HL_COLORS[] = {
		"#d63d3d", "#e8710a", "#e0a800", "#2f6fdb", "#4a3fc7",
		"#d63384", "#2f8f5b", "#9b9a97",
		"#fbe4e4", "#faebdd", "#fbf3db", "#ddedea", "#ddebf1",
		"#eae4f2", "#f4dfeb", "#e3e2e0" };

	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);
	v->setContentsMargins(0, 4, 0, 0);
	v->setSpacing(2);

	auto makeSwatch = [this](const QColor &c, bool foreground) -> QToolButton * {
		auto *b = new QToolButton(this);
		b->setFixedSize(20, 20);
		b->setCursor(Qt::PointingHandCursor);
		b->setToolTip(c.name());
		if (foreground) {
			b->setText(QStringLiteral("A"));
			b->setStyleSheet(QStringLiteral(
				"QToolButton{border:1px solid #c8c8c8;border-radius:3px;"
				"font-weight:bold;color:%1;background:white;}")
				.arg(c.name()));
			connect(b, &QToolButton::clicked, this,
				[this, c]() { applyTextForeground(c); });
		} else {
			b->setStyleSheet(QStringLiteral(
				"QToolButton{border:1px solid #c8c8c8;border-radius:10px;"
				"background:%1;}").arg(c.name()));
			connect(b, &QToolButton::clicked, this,
				[this, c]() { applyTextBackground(c); });
		}
		return b;
	};

	v->addWidget(new QLabel(tr("文字顏色"), w));
	auto *fg = new QHBoxLayout();
	fg->setSpacing(3);
	for (const char *hex : TEXT_COLORS)
		fg->addWidget(makeSwatch(QColor(QString::fromLatin1(hex)), true));
	fg->addStretch();
	v->addLayout(fg);

	v->addWidget(new QLabel(tr("文字底色(圓角標籤)"), w));
	auto *bg = new QGridLayout();
	bg->setSpacing(3);
	const int cols = 8;
	int n = 0;
	for (const char *hex : HL_COLORS) {
		bg->addWidget(makeSwatch(QColor(QString::fromLatin1(hex)), false),
			      n / cols, n % cols);
		++n;
	}
	auto *none = new QToolButton(this);
	none->setFixedSize(20, 20);
	none->setText(QStringLiteral("⊘"));
	none->setToolTip(tr("移除底色"));
	none->setCursor(Qt::PointingHandCursor);
	none->setStyleSheet(QStringLiteral(
		"QToolButton{border:1px solid #c8c8c8;border-radius:10px;"
		"background:white;}"));
	connect(none, &QToolButton::clicked, this,
		[this]() { applyTextBackground(QColor()); });
	bg->addWidget(none, n / cols, n % cols);
	auto *bg_wrap = new QHBoxLayout();
	bg_wrap->addLayout(bg);
	bg_wrap->addStretch();
	v->addLayout(bg_wrap);

	// 文字外框:圓角彩色框、白底黑字(顏色與底色同一組)
	v->addWidget(new QLabel(tr("文字外框(白底彩框)"), w));
	auto *ol = new QGridLayout();
	ol->setSpacing(3);
	int m = 0;
	for (const char *hex : HL_COLORS) {
		const QColor c(QString::fromLatin1(hex));
		auto *b = new QToolButton(this);
		b->setFixedSize(20, 20);
		b->setCursor(Qt::PointingHandCursor);
		b->setToolTip(c.name());
		b->setStyleSheet(QStringLiteral(
			"QToolButton{border:2px solid %1;border-radius:6px;"
			"background:white;}").arg(c.name()));
		connect(b, &QToolButton::clicked, this,
			[this, c]() { applyTextOutline(c); });
		ol->addWidget(b, m / cols, m % cols);
		++m;
	}
	auto *ol_none = new QToolButton(this);
	ol_none->setFixedSize(20, 20);
	ol_none->setText(QStringLiteral("⊘"));
	ol_none->setToolTip(tr("移除外框"));
	ol_none->setCursor(Qt::PointingHandCursor);
	ol_none->setStyleSheet(QStringLiteral(
		"QToolButton{border:1px solid #c8c8c8;border-radius:6px;"
		"background:white;}"));
	connect(ol_none, &QToolButton::clicked, this,
		[this]() { applyTextOutline(QColor()); });
	ol->addWidget(ol_none, m / cols, m % cols);
	auto *ol_wrap = new QHBoxLayout();
	ol_wrap->addLayout(ol);
	ol_wrap->addStretch();
	v->addLayout(ol_wrap);

	const int row = ui->gridLayout->rowCount();
	ui->gridLayout->addWidget(w, row, 0, 1, 4);
}

void IndiTextPropertiesWidget::on_m_bold_pb_clicked(bool checked)
{
	// 一律用項目字型旗標(不走 HTML 字元格式),維持純文字
	const QList<IndependentTextItem *> texts = editedTexts();
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
	// 一律用項目字型旗標(不走 HTML 字元格式),維持純文字
	const QList<IndependentTextItem *> texts = editedTexts();
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
