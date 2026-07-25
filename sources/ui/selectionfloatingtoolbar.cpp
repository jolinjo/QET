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
#include "selectionfloatingtoolbar.h"

#include "../QPropertyUndoCommand/qpropertyundocommand.h"
#include "../diagram.h"
#include "../qetgraphicsitem/diagramtextitem.h"

#include <QFont>
#include <QFrame>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QToolButton>
#include <QUndoStack>

SelectionFloatingToolbar::SelectionFloatingToolbar(QGraphicsView *view) :
	QFrame(view->viewport()),
	m_view(view)
{
	setFrameShape(QFrame::StyledPanel);
	setAutoFillBackground(true);
	setStyleSheet(QStringLiteral(
		"SelectionFloatingToolbar{background:white;border:1px solid #c8c8c8;"
		"border-radius:8px;}"
		"QToolButton{border:none;border-radius:4px;padding:2px;}"
		"QToolButton:hover{background:#ececf0;}"));
	m_layout = new QHBoxLayout(this);
	m_layout->setContentsMargins(6, 4, 6, 4);
	m_layout->setSpacing(2);
	hide();
}

void SelectionFloatingToolbar::clearContent()
{
	QLayoutItem *it = nullptr;
	while ((it = m_layout->takeAt(0)) != nullptr) {
		if (it->widget()) it->widget()->deleteLater();
		delete it;
	}
}

QToolButton *SelectionFloatingToolbar::addTextButton(const QString &text,
						     const QString &tip)
{
	auto *b = new QToolButton(this);
	b->setText(text);
	b->setToolTip(tip);
	b->setCursor(Qt::PointingHandCursor);
	b->setFixedHeight(24);
	m_layout->addWidget(b);
	return b;
}

void SelectionFloatingToolbar::addSeparator()
{
	auto *line = new QFrame(this);
	line->setFrameShape(QFrame::VLine);
	line->setFrameShadow(QFrame::Sunken);
	line->setFixedHeight(20);
	m_layout->addWidget(line);
}

void SelectionFloatingToolbar::addSwatch(const QColor &c, bool badge)
{
	auto *b = new QToolButton(this);
	b->setFixedSize(20, 20);
	b->setCursor(Qt::PointingHandCursor);
	b->setToolTip(c.isValid() ? c.name() : tr("移除底色"));
	if (!c.isValid()) {
		b->setText(QStringLiteral("⊘"));
		b->setStyleSheet(QStringLiteral(
			"QToolButton{border:1px solid #c8c8c8;border-radius:4px;"
			"background:white;}"));
		connect(b, &QToolButton::clicked, this,
			[this]() { applyBadge(QColor()); });
	} else if (badge) {
		b->setStyleSheet(QStringLiteral(
			"QToolButton{border:1px solid #c8c8c8;border-radius:4px;"
			"background:%1;}").arg(c.name()));
		connect(b, &QToolButton::clicked, this,
			[this, c]() { applyBadge(c); });
	} else {
		b->setText(QStringLiteral("A"));
		b->setStyleSheet(QStringLiteral(
			"QToolButton{border:1px solid #c8c8c8;border-radius:4px;"
			"font-weight:bold;color:%1;background:white;}").arg(c.name()));
		connect(b, &QToolButton::clicked, this,
			[this, c]() { applyColor(c); });
	}
	m_layout->addWidget(b);
}

void SelectionFloatingToolbar::showForText(DiagramTextItem *text)
{
	m_text = text;
	if (!text) { hide(); return; }
	clearContent();

	auto *b = addTextButton(QStringLiteral("B"), tr("粗體"));
	{ QFont f = b->font(); f.setBold(true); b->setFont(f); }
	connect(b, &QToolButton::clicked, this,
		&SelectionFloatingToolbar::toggleBold);
	auto *u = addTextButton(QStringLiteral("U"), tr("底線"));
	{ QFont f = u->font(); f.setUnderline(true); u->setFont(f); }
	connect(u, &QToolButton::clicked, this,
		&SelectionFloatingToolbar::toggleUnderline);

	addSeparator();
	// 文字色票
	static const char *const TEXT_COLORS[] = {
		"#1a1a1a", "#e03e3e", "#d9730d", "#0f7b6c", "#0b6e99", "#6940a5" };
	for (const char *hex : TEXT_COLORS)
		addSwatch(QColor(QString::fromLatin1(hex)), false);

	addSeparator();
	// badge 底色色票 + 移除
	static const char *const BADGE_COLORS[] = {
		"#d63d3d", "#e8710a", "#e0a800", "#2f6fdb", "#2f8f5b", "#9b9a97" };
	for (const char *hex : BADGE_COLORS)
		addSwatch(QColor(QString::fromLatin1(hex)), true);
	addSwatch(QColor(), true);   // ⊘ 移除底色

	reposition();
}

void SelectionFloatingToolbar::reposition()
{
	if (!m_text || !m_text->scene() || !m_view) { hide(); return; }
	adjustSize();
	const QRectF sr = m_text->sceneBoundingRect();
	const QPoint top = m_view->mapFromScene(
		QPointF(sr.center().x(), sr.top()));
	int x = top.x() - width() / 2;
	int y = top.y() - height() - 8;
	if (y < 2) {   // 上方沒空間 → 放到項目下方
		const QPoint bottom = m_view->mapFromScene(
			QPointF(sr.center().x(), sr.bottom()));
		y = bottom.y() + 8;
	}
	const int vw = m_view->viewport()->width();
	x = qBound(2, x, qMax(2, vw - width() - 2));
	move(x, y);
	show();
	raise();
}

/* ── 套用 ────────────────────────────────────────────────────────── */

void SelectionFloatingToolbar::toggleBold()
{
	if (!m_text || !m_text->diagram()) return;
	QFont f = m_text->font();
	f.setBold(!f.bold());
	m_text->diagram()->undoStack().push(new QPropertyUndoCommand(
		m_text, "font", QVariant(m_text->font()), QVariant(f)));
}

void SelectionFloatingToolbar::toggleUnderline()
{
	if (!m_text || !m_text->diagram()) return;
	QFont f = m_text->font();
	f.setUnderline(!f.underline());
	m_text->diagram()->undoStack().push(new QPropertyUndoCommand(
		m_text, "font", QVariant(m_text->font()), QVariant(f)));
}

void SelectionFloatingToolbar::applyColor(const QColor &c)
{
	if (!m_text || !m_text->diagram() || !c.isValid()) return;
	if (m_text->color() == c) return;
	m_text->diagram()->undoStack().push(new QPropertyUndoCommand(
		m_text, "color", QVariant(m_text->color()), QVariant(c)));
}

void SelectionFloatingToolbar::applyBadge(const QColor &c)
{
	if (!m_text || !m_text->diagram()) return;
	QUndoStack &st = m_text->diagram()->undoStack();
	st.beginMacro(c.isValid() ? tr("設定文字底色") : tr("移除文字底色"));
	st.push(new QPropertyUndoCommand(m_text, "badgeBackground",
		QVariant(m_text->badgeBackground()), QVariant(c)));
	if (c.isValid()) {
		const int yiq = (c.red() * 299 + c.green() * 587
				 + c.blue() * 114) / 1000;
		const QColor fg = yiq < 150
			? QColor(Qt::white) : QColor(0x1a, 0x1a, 0x1a);
		if (m_text->color() != fg)
			st.push(new QPropertyUndoCommand(m_text, "color",
				QVariant(m_text->color()), QVariant(fg)));
	}
	st.endMacro();
}
