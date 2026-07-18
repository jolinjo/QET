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
#include "diagramtableitem.h"

#include "../QPropertyUndoCommand/qpropertyundocommand.h"
#include "../QetGraphicsItemModeler/qetgraphicshandleritem.h"
#include "../diagram.h"

#include <QDomElement>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPainter>

namespace {
	const qreal MIN_COL = 20;
	const qreal MIN_ROW = 12;
	const qreal DEF_COL = 80;
	const qreal DEF_ROW = 24;
}

DiagramTableItem::DiagramTableItem(QetGraphicsItem *parent) :
	QetGraphicsItem(parent)
{
	setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable
		 | QGraphicsItem::ItemSendsGeometryChanges);
}

DiagramTableItem::~DiagramTableItem()
{
	removeHandlers();
}

void DiagramTableItem::setup(int rows, int cols)
{
	m_rows = qMax(1, rows);
	m_cols = qMax(1, cols);
	m_row_height = DEF_ROW;
	m_col_widths = QVector<qreal>(m_cols, DEF_COL);
	m_cells = QVector<QString>(m_rows * m_cols);
	prepareGeometryChange();
	update();
}

/* ── 序列化(供 undo / 存檔)────────────────────────────────────── */

QString DiagramTableItem::state() const
{
	QJsonObject o;
	o[QStringLiteral("rows")] = m_rows;
	o[QStringLiteral("cols")] = m_cols;
	o[QStringLiteral("row_height")] = m_row_height;
	o[QStringLiteral("font")] = m_font.toString();
	QJsonArray w;
	for (qreal v : m_col_widths) w.append(v);
	o[QStringLiteral("widths")] = w;
	QJsonArray c;
	for (const QString &s : m_cells) c.append(s);
	o[QStringLiteral("cells")] = c;
	return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void DiagramTableItem::setState(const QString &s)
{
	const QJsonObject o = QJsonDocument::fromJson(s.toUtf8()).object();
	if (o.isEmpty()) return;
	prepareGeometryChange();
	m_rows = o.value(QStringLiteral("rows")).toInt(1);
	m_cols = o.value(QStringLiteral("cols")).toInt(1);
	m_row_height = o.value(QStringLiteral("row_height")).toDouble(DEF_ROW);
	if (o.contains(QStringLiteral("font")))
		m_font.fromString(o.value(QStringLiteral("font")).toString());
	m_col_widths.clear();
	for (const QJsonValue &v : o.value(QStringLiteral("widths")).toArray())
		m_col_widths.append(v.toDouble(DEF_COL));
	m_col_widths.resize(m_cols);
	m_cells.clear();
	for (const QJsonValue &v : o.value(QStringLiteral("cells")).toArray())
		m_cells.append(v.toString());
	m_cells.resize(m_rows * m_cols);
	if (isSelected()) { removeHandlers(); addHandlers(); }
	update();
}

/* ── 幾何 ────────────────────────────────────────────────────────── */

qreal DiagramTableItem::tableWidth() const
{
	qreal w = 0;
	for (qreal v : m_col_widths) w += v;
	return w;
}

qreal DiagramTableItem::tableHeight() const
{
	return m_row_height * m_rows;
}

qreal DiagramTableItem::columnLeft(int col) const
{
	qreal x = 0;
	for (int i = 0; i < col && i < m_col_widths.size(); ++i)
		x += m_col_widths.at(i);
	return x;
}

QRectF DiagramTableItem::boundingRect() const
{
	return QRectF(0, 0, tableWidth(), tableHeight());
}

int DiagramTableItem::cellAt(const QPointF &p, int *row, int *col) const
{
	if (p.y() < 0 || p.y() >= tableHeight()) return -1;
	const int r = int(p.y() / m_row_height);
	qreal x = 0;
	for (int c = 0; c < m_cols; ++c) {
		if (p.x() >= x && p.x() < x + m_col_widths.at(c)) {
			*row = qBound(0, r, m_rows - 1);
			*col = c;
			return r * m_cols + c;
		}
		x += m_col_widths.at(c);
	}
	return -1;
}

/* ── 繪製 ────────────────────────────────────────────────────────── */

void DiagramTableItem::paint(QPainter *painter,
			     const QStyleOptionGraphicsItem *, QWidget *)
{
	const qreal W = tableWidth();
	const qreal H = tableHeight();

	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, false);
	painter->fillRect(QRectF(0, 0, W, H), Qt::white);

	QPen pen(Qt::black);
	pen.setCosmetic(true);
	painter->setPen(pen);
	painter->drawRect(QRectF(0, 0, W, H));
	// 直線(欄界)
	qreal x = 0;
	for (int c = 0; c < m_cols - 1; ++c) {
		x += m_col_widths.at(c);
		painter->drawLine(QPointF(x, 0), QPointF(x, H));
	}
	// 橫線(列界)
	for (int r = 1; r < m_rows; ++r)
		painter->drawLine(QPointF(0, r * m_row_height),
				  QPointF(W, r * m_row_height));

	// 儲存格文字
	painter->setFont(m_font);
	for (int r = 0; r < m_rows; ++r) {
		for (int c = 0; c < m_cols; ++c) {
			const QString &txt = m_cells.at(r * m_cols + c);
			if (txt.isEmpty()) continue;
			QRectF cell(columnLeft(c), r * m_row_height,
				    m_col_widths.at(c), m_row_height);
			painter->drawText(cell.adjusted(3, 1, -3, -1),
				Qt::AlignVCenter | Qt::AlignLeft, txt);
		}
	}
	painter->restore();

	if (isSelected()) {
		painter->save();
		QPen sp(Qt::black);
		sp.setStyle(Qt::DashLine);
		sp.setCosmetic(true);
		painter->setPen(sp);
		painter->setBrush(Qt::NoBrush);
		painter->drawRect(boundingRect());
		painter->restore();
	}
}

/* ── 控制點(每欄右緣 + 列高)──────────────────────────────────── */

void DiagramTableItem::addHandlers()
{
	if (!m_handlers.isEmpty() || !scene()) return;
	QVector<QPointF> pts;
	for (int c = 0; c < m_cols; ++c)               // 每欄右緣(調欄寬)
		pts << QPointF(columnLeft(c) + m_col_widths.at(c),
			       tableHeight() / 2);
	pts << QPointF(tableWidth() / 2, tableHeight()); // 底部(統一列高)
	m_handlers = QetGraphicsHandlerItem::handlerForPoint(mapToScene(pts));
	for (int i = 0; i < m_handlers.size(); ++i) {
		QetGraphicsHandlerItem *h = m_handlers.at(i);
		h->setZValue(zValue() + 1);
		h->setColor(i == m_handlers.size() - 1 ? Qt::darkGreen : Qt::blue);
		scene()->addItem(h);
		h->installSceneEventFilter(this);
	}
}

void DiagramTableItem::removeHandlers()
{
	if (!m_handlers.isEmpty()) {
		qDeleteAll(m_handlers);
		m_handlers.clear();
	}
}

void DiagramTableItem::adjustHandlerPos()
{
	if (m_handlers.isEmpty()) return;
	QVector<QPointF> pts;
	for (int c = 0; c < m_cols; ++c)
		pts << QPointF(columnLeft(c) + m_col_widths.at(c),
			       tableHeight() / 2);
	pts << QPointF(tableWidth() / 2, tableHeight());
	const QPolygonF sp = mapToScene(pts);
	if (m_handlers.size() == sp.size())
		for (int i = 0; i < m_handlers.size(); ++i)
			m_handlers.at(i)->setPos(sp.at(i));
}

QVariant DiagramTableItem::itemChange(GraphicsItemChange change,
				      const QVariant &value)
{
	if (change == ItemSelectedHasChanged) {
		if (value.toBool() && scene()) addHandlers();
		else { commitEditor(); removeHandlers(); }
	} else if (change == ItemPositionHasChanged
		   || change == ItemTransformHasChanged) {
		adjustHandlerPos();
	}
	return QetGraphicsItem::itemChange(change, value);
}

bool DiagramTableItem::sceneEventFilter(QGraphicsItem *watched, QEvent *event)
{
	if (watched->type() != QetGraphicsHandlerItem::Type) return false;
	auto *h = qgraphicsitem_cast<QetGraphicsHandlerItem *>(watched);
	if (!m_handlers.contains(h)) return false;
	m_active = m_handlers.indexOf(h);

	if (event->type() == QEvent::GraphicsSceneMousePress) {
		m_drag_old_state = state();
		return true;
	}
	if (event->type() == QEvent::GraphicsSceneMouseMove) {
		const QPointF p = mapFromScene(
			static_cast<QGraphicsSceneMouseEvent *>(event)->scenePos());
		prepareGeometryChange();
		if (m_active >= 0 && m_active < m_cols) {          // 調某欄寬
			m_col_widths[m_active] =
				qMax(MIN_COL, p.x() - columnLeft(m_active));
		} else {                                           // 統一列高
			m_row_height = qMax(MIN_ROW, p.y() / m_rows);
		}
		adjustHandlerPos();
		update();
		return true;
	}
	if (event->type() == QEvent::GraphicsSceneMouseRelease) {
		pushStateUndo(m_drag_old_state);
		return true;
	}
	return false;
}

/* ── 儲存格編輯 ─────────────────────────────────────────────────── */

void DiagramTableItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
	int r = 0, c = 0;
	if (cellAt(event->pos(), &r, &c) >= 0) {
		editCell(r, c);
		event->accept();
		return;
	}
	QetGraphicsItem::mouseDoubleClickEvent(event);
}

void DiagramTableItem::editCell(int row, int col)
{
	if (diagram() && diagram()->isReadOnly()) return;
	const int idx = row * m_cols + col;
	if (idx < 0 || idx >= m_cells.size()) return;
	commitEditor();   // 若已有開啟中的編輯框先收合

	auto *le = new QLineEdit();
	le->setText(m_cells.at(idx));
	le->setFrame(false);
	le->setStyleSheet(QStringLiteral("background:white;"));

	m_editor = new QGraphicsProxyWidget(this);
	m_editor->setWidget(le);
	m_editor->setGeometry(QRectF(columnLeft(col), row * m_row_height,
				     m_col_widths.at(col), m_row_height));
	m_editor->setZValue(zValue() + 2);
	m_edit_index = idx;

	le->selectAll();
	m_editor->setFocus();
	le->setFocus();
	// Enter 或失焦即寫回
	connect(le, &QLineEdit::editingFinished, this,
		[this]() { commitEditor(); });
}

void DiagramTableItem::commitEditor()
{
	if (!m_editor) return;
	// 先摘下指標,避免刪除 QLineEdit 觸發的 editingFinished 造成重入
	QGraphicsProxyWidget *ed = m_editor;
	const int idx = m_edit_index;
	m_editor = nullptr;
	m_edit_index = -1;

	QString text;
	if (auto *le = qobject_cast<QLineEdit *>(ed->widget()))
		text = le->text();
	ed->deleteLater();

	if (idx >= 0 && idx < m_cells.size() && text != m_cells.at(idx)) {
		const QString old = state();
		m_cells[idx] = text;
		update();
		pushStateUndo(old);
	}
}

void DiagramTableItem::pushStateUndo(const QString &old_state)
{
	if (!diagram() || old_state == state()) return;
	auto *undo = new QPropertyUndoCommand(this, "state",
					      old_state, state());
	undo->setText(tr("Modifier %1").arg(name()));
	diagram()->undoStack().push(undo);
}

/* ── XML ─────────────────────────────────────────────────────────── */

bool DiagramTableItem::fromXml(const QDomElement &e)
{
	if (e.tagName() != QLatin1String("drawing_table")) return false;
	setState(e.attribute(QStringLiteral("state")));
	QGraphicsObject::setPos(e.attribute(QStringLiteral("x")).toDouble(),
				e.attribute(QStringLiteral("y")).toDouble());
	setZValue(e.attribute(QStringLiteral("z"), QString::number(zValue()))
		  .toDouble());
	return true;
}

QDomElement DiagramTableItem::toXml(QDomDocument &doc) const
{
	QDomElement e = doc.createElement(QStringLiteral("drawing_table"));
	e.setAttribute(QStringLiteral("x"), QString::number(pos().x()));
	e.setAttribute(QStringLiteral("y"), QString::number(pos().y()));
	e.setAttribute(QStringLiteral("z"), QString::number(zValue()));
	e.setAttribute(QStringLiteral("state"), state());
	return e;
}
