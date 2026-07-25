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
	const qreal HDR = 13;   // 選取列/欄把手區(表格上方、左方)厚度
	const qreal ADD = 14;   // 新增列/欄「+」區(表格下方、右方)厚度
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
	m_cell_bg = QVector<QColor>(m_rows * m_cols);   // 預設無效色 = 不填
	m_cell_halign = QVector<int>(m_rows * m_cols, int(Qt::AlignLeft));
	m_cell_valign = QVector<int>(m_rows * m_cols, int(Qt::AlignVCenter));
	m_cell_size = QVector<int>(m_rows * m_cols, 0);   // 0 = 用預設字級
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
	QJsonArray bg;
	for (const QColor &col : m_cell_bg)
		bg.append(col.isValid() ? col.name(QColor::HexArgb) : QString());
	o[QStringLiteral("cell_bg")] = bg;
	QJsonArray ha, va, sz;
	for (int v : m_cell_halign) ha.append(v);
	for (int v : m_cell_valign) va.append(v);
	for (int v : m_cell_size)   sz.append(v);
	o[QStringLiteral("cell_halign")] = ha;
	o[QStringLiteral("cell_valign")] = va;
	o[QStringLiteral("cell_size")]   = sz;
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
	m_cell_bg.clear();
	for (const QJsonValue &v : o.value(QStringLiteral("cell_bg")).toArray()) {
		const QString s = v.toString();
		m_cell_bg.append(s.isEmpty() ? QColor() : QColor(s));
	}
	m_cell_bg.resize(m_rows * m_cols);
	// 逐格對齊/字級。向後相容:舊檔以整表 align/valign 純量套用到全部格。
	const int n = m_rows * m_cols;
	const int old_h = o.value(QStringLiteral("align"))
		.toInt(int(Qt::AlignLeft));
	const int old_v = o.value(QStringLiteral("valign"))
		.toInt(int(Qt::AlignVCenter));
	m_cell_halign.clear();
	for (const QJsonValue &v : o.value(QStringLiteral("cell_halign")).toArray())
		m_cell_halign.append(v.toInt(int(Qt::AlignLeft)));
	if (m_cell_halign.isEmpty()) m_cell_halign = QVector<int>(n, old_h);
	m_cell_halign.resize(n);
	m_cell_valign.clear();
	for (const QJsonValue &v : o.value(QStringLiteral("cell_valign")).toArray())
		m_cell_valign.append(v.toInt(int(Qt::AlignVCenter)));
	if (m_cell_valign.isEmpty()) m_cell_valign = QVector<int>(n, old_v);
	m_cell_valign.resize(n);
	m_cell_size.clear();
	for (const QJsonValue &v : o.value(QStringLiteral("cell_size")).toArray())
		m_cell_size.append(v.toInt(0));
	m_cell_size.resize(n);
	// 表結構可能改變(undo/還原):清掉可能越界的選取
	m_sel_r0 = m_sel_c0 = m_sel_r1 = m_sel_c1 = -1;
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
	const qreal W = tableWidth(), H = tableHeight();
	// 選取時外擴,容納選列/欄把手與新增列/欄的「+」affordance
	if (isSelected())
		return QRectF(-HDR, -HDR, W + HDR + ADD, H + HDR + ADD);
	return QRectF(0, 0, W, H);
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

	// 每格底色(有效色才填)
	for (int r = 0; r < m_rows; ++r) {
		for (int c = 0; c < m_cols; ++c) {
			const int i = r * m_cols + c;
			if (i >= m_cell_bg.size() || !m_cell_bg.at(i).isValid())
				continue;
			painter->fillRect(QRectF(columnLeft(c), r * m_row_height,
					  m_col_widths.at(c), m_row_height),
					  m_cell_bg.at(i));
		}
	}

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

	// 儲存格文字(逐格字級/對齊)
	for (int r = 0; r < m_rows; ++r) {
		for (int c = 0; c < m_cols; ++c) {
			const int i = r * m_cols + c;
			const QString &txt = m_cells.at(i);
			if (txt.isEmpty()) continue;
			QFont f = m_font;
			const int sz = m_cell_size.value(i, 0);
			if (sz > 0) f.setPointSize(sz);
			painter->setFont(f);
			const Qt::Alignment al =
				Qt::Alignment(m_cell_valign.value(i,
					int(Qt::AlignVCenter)))
				| Qt::Alignment(m_cell_halign.value(i,
					int(Qt::AlignLeft)));
			QRectF cell(columnLeft(c), r * m_row_height,
				    m_col_widths.at(c), m_row_height);
			painter->drawText(cell.adjusted(3, 1, -3, -1), al, txt);
		}
	}
	painter->restore();

	// 選取範圍反白(半透明藍 + 藍框)
	int r0, c0, r1, c1;
	if (selectionRect(&r0, &c0, &r1, &c1)) {
		const qreal x0 = columnLeft(c0);
		qreal x1 = columnLeft(c1) + m_col_widths.at(c1);
		const QRectF sel(x0, r0 * m_row_height,
				 x1 - x0, (r1 - r0 + 1) * m_row_height);
		painter->save();
		painter->setRenderHint(QPainter::Antialiasing, false);
		painter->fillRect(sel, QColor(0x2d, 0x7d, 0xff, 60));
		QPen selp(QColor(0x2d, 0x7d, 0xff));
		selp.setCosmetic(true);
		selp.setWidth(2);
		painter->setPen(selp);
		painter->setBrush(Qt::NoBrush);
		painter->drawRect(sel);
		painter->restore();
	}

	if (isSelected()) {
		painter->save();
		QPen sp(Qt::black);
		sp.setStyle(Qt::DashLine);
		sp.setCosmetic(true);
		painter->setPen(sp);
		painter->setBrush(Qt::NoBrush);
		painter->drawRect(QRectF(0, 0, W, H));
		painter->restore();

		// ── Notion 風 affordance(僅選取時顯示)──────────────
		painter->save();
		painter->setRenderHint(QPainter::Antialiasing, true);
		const QColor hdr(0x2d, 0x7d, 0xff);   // 焦點列/欄的把手:藍色
		const QColor dot(0xff, 0xff, 0xff);
		painter->setPen(Qt::NoPen);

		// 選欄/選列把手只畫在「目前有焦點(藍色選取)」的列/欄上,
		// 不是每列每欄都畫(先點一格,該列/欄才出現把手)。
		int sr0, sc0, sr1, sc1;
		if (selectionRect(&sr0, &sc0, &sr1, &sc1)) {
			for (int c = sc0; c <= sc1; ++c) {   // 該欄上方
				const QRectF h(columnLeft(c) + 1, -HDR + 1,
					       m_col_widths.at(c) - 2, HDR - 2);
				painter->setBrush(hdr);
				painter->drawRoundedRect(h, 3, 3);
				painter->setBrush(dot);
				const qreal cx = h.center().x(), cy = h.center().y();
				for (int i = -1; i <= 1; ++i)
					painter->drawEllipse(QPointF(cx + i * 4, cy),
							     1.1, 1.1);
			}
			for (int r = sr0; r <= sr1; ++r) {   // 該列左方
				const QRectF h(-HDR + 1, r * m_row_height + 1,
					       HDR - 2, m_row_height - 2);
				painter->setBrush(hdr);
				painter->drawRoundedRect(h, 3, 3);
				painter->setBrush(dot);
				const qreal cx = h.center().x(), cy = h.center().y();
				for (int i = -1; i <= 1; ++i)
					painter->drawEllipse(QPointF(cx, cy + i * 4),
							     1.1, 1.1);
			}
		}
		// 下方「+」= 新增一列;右方「+」= 新增一欄
		auto plusBar = [&](const QRectF &bar) {
			painter->setBrush(QColor(0xec, 0xec, 0xf0));
			painter->drawRoundedRect(bar, 3, 3);
			QPen pp(QColor(0x70, 0x70, 0x78));
			pp.setWidthF(1.4);
			pp.setCosmetic(true);
			painter->setPen(pp);
			const qreal cx = bar.center().x(), cy = bar.center().y();
			painter->drawLine(QPointF(cx - 4, cy), QPointF(cx + 4, cy));
			painter->drawLine(QPointF(cx, cy - 4), QPointF(cx, cy + 4));
			painter->setPen(Qt::NoPen);
		};
		plusBar(QRectF(0, H + 2, W, ADD - 3));
		plusBar(QRectF(W + 2, 0, ADD - 3, H));
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
		prepareGeometryChange();   // boundingRect 依 isSelected() 改變
		if (value.toBool() && scene()) addHandlers();
		else { commitEditor(); removeHandlers(); clearCellSelection(); }
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

/* ── 儲存格選取 ─────────────────────────────────────────────────── */

bool DiagramTableItem::selectionRect(int *r0, int *c0, int *r1, int *c1) const
{
	if (m_sel_r0 < 0 || m_sel_c0 < 0 || m_sel_r1 < 0 || m_sel_c1 < 0)
		return false;
	*r0 = qBound(0, qMin(m_sel_r0, m_sel_r1), m_rows - 1);
	*r1 = qBound(0, qMax(m_sel_r0, m_sel_r1), m_rows - 1);
	*c0 = qBound(0, qMin(m_sel_c0, m_sel_c1), m_cols - 1);
	*c1 = qBound(0, qMax(m_sel_c0, m_sel_c1), m_cols - 1);
	return true;
}

bool DiagramTableItem::hasCellSelection() const
{
	int a, b, c, d;
	return selectionRect(&a, &b, &c, &d);
}

void DiagramTableItem::setSelectionAnchor(int row, int col)
{
	m_sel_r0 = m_sel_r1 = qBound(0, row, m_rows - 1);
	m_sel_c0 = m_sel_c1 = qBound(0, col, m_cols - 1);
	update();
	emit tableSelectionChanged();
}

void DiagramTableItem::extendSelectionTo(int row, int col)
{
	m_sel_r1 = qBound(0, row, m_rows - 1);
	m_sel_c1 = qBound(0, col, m_cols - 1);
	update();
	emit tableSelectionChanged();
}

void DiagramTableItem::clearCellSelection()
{
	if (m_sel_r0 == -1 && m_sel_c0 == -1) return;
	m_sel_r0 = m_sel_c0 = m_sel_r1 = m_sel_c1 = -1;
	update();
	emit tableSelectionChanged();
}

void DiagramTableItem::selectAllCells()
{
	m_sel_r0 = 0; m_sel_c0 = 0;
	m_sel_r1 = m_rows - 1; m_sel_c1 = m_cols - 1;
	update();
	emit tableSelectionChanged();
}

void DiagramTableItem::selectFullRows()
{
	int r0, c0, r1, c1;
	if (!selectionRect(&r0, &c0, &r1, &c1)) { selectAllCells(); return; }
	m_sel_r0 = r0; m_sel_r1 = r1;
	m_sel_c0 = 0;  m_sel_c1 = m_cols - 1;
	update();
	emit tableSelectionChanged();
}

void DiagramTableItem::selectFullColumns()
{
	int r0, c0, r1, c1;
	if (!selectionRect(&r0, &c0, &r1, &c1)) { selectAllCells(); return; }
	m_sel_c0 = c0; m_sel_c1 = c1;
	m_sel_r0 = 0;  m_sel_r1 = m_rows - 1;
	update();
	emit tableSelectionChanged();
}

void DiagramTableItem::setSelectionBackground(const QColor &color)
{
	if (diagram() && diagram()->isReadOnly()) return;
	int r0, c0, r1, c1;
	const bool has_sel = selectionRect(&r0, &c0, &r1, &c1);
	if (!has_sel) { r0 = 0; c0 = 0; r1 = m_rows - 1; c1 = m_cols - 1; }

	if (m_cell_bg.size() != m_rows * m_cols)
		m_cell_bg.resize(m_rows * m_cols);
	const QString old = state();
	for (int r = r0; r <= r1; ++r)
		for (int c = c0; c <= c1; ++c)
			m_cell_bg[r * m_cols + c] = color;
	update();
	pushStateUndo(old);
}

void DiagramTableItem::selectWholeRow(int row)
{
	m_sel_r0 = m_sel_r1 = qBound(0, row, m_rows - 1);
	m_sel_c0 = 0; m_sel_c1 = m_cols - 1;
	update();
	emit tableSelectionChanged();
}

void DiagramTableItem::selectWholeColumn(int col)
{
	m_sel_c0 = m_sel_c1 = qBound(0, col, m_cols - 1);
	m_sel_r0 = 0; m_sel_r1 = m_rows - 1;
	update();
	emit tableSelectionChanged();
}

void DiagramTableItem::addRowAtEnd()
{
	if (diagram() && diagram()->isReadOnly()) return;
	const QString old = state();
	prepareGeometryChange();
	for (int c = 0; c < m_cols; ++c) {
		m_cells.append(QString());
		m_cell_bg.append(QColor());
		m_cell_halign.append(int(Qt::AlignLeft));
		m_cell_valign.append(int(Qt::AlignVCenter));
		m_cell_size.append(0);
	}
	++m_rows;
	if (isSelected()) { removeHandlers(); addHandlers(); }
	update();
	pushStateUndo(old);
}

void DiagramTableItem::addColumnAtEnd()
{
	if (diagram() && diagram()->isReadOnly()) return;
	const QString old = state();
	prepareGeometryChange();
	QVector<QString> nc;
	QVector<QColor> nbg;
	QVector<int> nha, nva, nsz;
	for (int r = 0; r < m_rows; ++r) {
		for (int c = 0; c < m_cols; ++c) {
			const int i = r * m_cols + c;
			nc.append(m_cells.value(i));
			nbg.append(m_cell_bg.value(i));
			nha.append(m_cell_halign.value(i, int(Qt::AlignLeft)));
			nva.append(m_cell_valign.value(i, int(Qt::AlignVCenter)));
			nsz.append(m_cell_size.value(i, 0));
		}
		nc.append(QString());   // 新欄
		nbg.append(QColor());
		nha.append(int(Qt::AlignLeft));
		nva.append(int(Qt::AlignVCenter));
		nsz.append(0);
	}
	m_col_widths.append(DEF_COL);
	++m_cols;
	m_cells = nc;
	m_cell_bg = nbg;
	m_cell_halign = nha; m_cell_valign = nva; m_cell_size = nsz;
	if (isSelected()) { removeHandlers(); addHandlers(); }
	update();
	pushStateUndo(old);
}

void DiagramTableItem::deleteSelectedRows()
{
	if (diagram() && diagram()->isReadOnly()) return;
	int r0, c0, r1, c1;
	if (!selectionRect(&r0, &c0, &r1, &c1)) { r0 = r1 = m_rows - 1; }
	int count = r1 - r0 + 1;
	if (count >= m_rows) count = m_rows - 1;   // 至少保留一列
	if (count <= 0) return;
	const QString old = state();
	prepareGeometryChange();
	QVector<QString> nc;
	QVector<QColor> nbg;
	QVector<int> nha, nva, nsz;
	for (int r = 0; r < m_rows; ++r) {
		if (r >= r0 && r < r0 + count) continue;
		for (int c = 0; c < m_cols; ++c) {
			const int i = r * m_cols + c;
			nc.append(m_cells.value(i));
			nbg.append(m_cell_bg.value(i));
			nha.append(m_cell_halign.value(i, int(Qt::AlignLeft)));
			nva.append(m_cell_valign.value(i, int(Qt::AlignVCenter)));
			nsz.append(m_cell_size.value(i, 0));
		}
	}
	m_rows -= count;
	m_cells = nc; m_cell_bg = nbg;
	m_cell_halign = nha; m_cell_valign = nva; m_cell_size = nsz;
	clearCellSelection();
	if (isSelected()) { removeHandlers(); addHandlers(); }
	update();
	pushStateUndo(old);
}

void DiagramTableItem::deleteSelectedColumns()
{
	if (diagram() && diagram()->isReadOnly()) return;
	int r0, c0, r1, c1;
	if (!selectionRect(&r0, &c0, &r1, &c1)) { c0 = c1 = m_cols - 1; }
	int count = c1 - c0 + 1;
	if (count >= m_cols) count = m_cols - 1;   // 至少保留一欄
	if (count <= 0) return;
	const QString old = state();
	prepareGeometryChange();
	QVector<QString> nc;
	QVector<QColor> nbg;
	QVector<qreal> nw;
	QVector<int> nha, nva, nsz;
	for (int c = 0; c < m_cols; ++c)
		if (!(c >= c0 && c < c0 + count)) nw.append(m_col_widths.value(c));
	for (int r = 0; r < m_rows; ++r)
		for (int c = 0; c < m_cols; ++c)
			if (!(c >= c0 && c < c0 + count)) {
				const int i = r * m_cols + c;
				nc.append(m_cells.value(i));
				nbg.append(m_cell_bg.value(i));
				nha.append(m_cell_halign.value(i, int(Qt::AlignLeft)));
				nva.append(m_cell_valign.value(i,
					int(Qt::AlignVCenter)));
				nsz.append(m_cell_size.value(i, 0));
			}
	m_cols -= count;
	m_col_widths = nw; m_cells = nc; m_cell_bg = nbg;
	m_cell_halign = nha; m_cell_valign = nva; m_cell_size = nsz;
	clearCellSelection();
	if (isSelected()) { removeHandlers(); addHandlers(); }
	update();
	pushStateUndo(old);
}

void DiagramTableItem::setSelectionAlignH(Qt::Alignment h)
{
	if (diagram() && diagram()->isReadOnly()) return;
	int r0, c0, r1, c1;
	if (!selectionRect(&r0, &c0, &r1, &c1)) {
		r0 = 0; c0 = 0; r1 = m_rows - 1; c1 = m_cols - 1;
	}
	if (m_cell_halign.size() != m_rows * m_cols)
		m_cell_halign.resize(m_rows * m_cols);
	const QString old = state();
	for (int r = r0; r <= r1; ++r)
		for (int c = c0; c <= c1; ++c)
			m_cell_halign[r * m_cols + c] = int(h);
	update();
	pushStateUndo(old);
}

void DiagramTableItem::setSelectionValign(Qt::Alignment v)
{
	if (diagram() && diagram()->isReadOnly()) return;
	int r0, c0, r1, c1;
	if (!selectionRect(&r0, &c0, &r1, &c1)) {
		r0 = 0; c0 = 0; r1 = m_rows - 1; c1 = m_cols - 1;
	}
	if (m_cell_valign.size() != m_rows * m_cols)
		m_cell_valign.resize(m_rows * m_cols);
	const QString old = state();
	for (int r = r0; r <= r1; ++r)
		for (int c = c0; c <= c1; ++c)
			m_cell_valign[r * m_cols + c] = int(v);
	update();
	pushStateUndo(old);
}

void DiagramTableItem::setSelectionFontSize(int pt)
{
	if (pt <= 0 || (diagram() && diagram()->isReadOnly())) return;
	int r0, c0, r1, c1;
	if (!selectionRect(&r0, &c0, &r1, &c1)) {
		r0 = 0; c0 = 0; r1 = m_rows - 1; c1 = m_cols - 1;
	}
	if (m_cell_size.size() != m_rows * m_cols)
		m_cell_size.resize(m_rows * m_cols);
	const QString old = state();
	for (int r = r0; r <= r1; ++r)
		for (int c = c0; c <= c1; ++c)
			m_cell_size[r * m_cols + c] = pt;
	update();
	pushStateUndo(old);
}

int DiagramTableItem::currentFontSize() const
{
	int r0, c0, r1, c1, idx = 0;
	if (selectionRect(&r0, &c0, &r1, &c1)) idx = r0 * m_cols + c0;
	const int sz = m_cell_size.value(idx, 0);
	if (sz > 0) return sz;
	return m_font.pointSize() > 0 ? m_font.pointSize() : 9;
}

bool DiagramTableItem::hitAffordance(const QPointF &p)
{
	const qreal W = tableWidth(), H = tableHeight();
	// 下方「+」= 新增一列
	if (p.y() >= H && p.y() <= H + ADD && p.x() >= 0 && p.x() <= W) {
		addRowAtEnd(); return true;
	}
	// 右方「+」= 新增一欄
	if (p.x() >= W && p.x() <= W + ADD && p.y() >= 0 && p.y() <= H) {
		addColumnAtEnd(); return true;
	}
	// 上方把手 = 選整欄
	if (p.y() >= -HDR && p.y() < 0 && p.x() >= 0 && p.x() < W) {
		qreal x = 0; int c = 0;
		for (c = 0; c < m_cols; ++c) {
			if (p.x() < x + m_col_widths.at(c)) break;
			x += m_col_widths.at(c);
		}
		selectWholeColumn(c);
		return true;
	}
	// 左方把手 = 選整列
	if (p.x() >= -HDR && p.x() < 0 && p.y() >= 0 && p.y() < H) {
		selectWholeRow(int(p.y() / m_row_height));
		return true;
	}
	return false;
}

/* ── 滑鼠(選取儲存格 / 移動)──────────────────────────────────── */

void DiagramTableItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
	// 已選取時,先判定是否點到把手/「+」affordance
	if (isSelected() && event->button() == Qt::LeftButton
	    && hitAffordance(event->pos())) {
		event->accept();
		return;
	}
	m_press_pos = event->pos();
	m_press_r = m_press_c = -1;
	m_press_was_selected = isSelected();
	int r = 0, c = 0;
	const bool on_cell =
		(event->button() == Qt::LeftButton)
		&& cellAt(event->pos(), &r, &c) >= 0;
	if (on_cell) { m_press_r = r; m_press_c = c; }

	// Shift+拖曳 = 矩形範圍選取(不移動表格)
	if (on_cell && (event->modifiers() & Qt::ShiftModifier) && isSelected()) {
		m_selecting = true;
		if (m_sel_r0 < 0) setSelectionAnchor(r, c);
		else extendSelectionTo(r, c);
		event->accept();
		return;
	}
	QetGraphicsItem::mousePressEvent(event);   // 正常選取/移動
}

void DiagramTableItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
	if (m_selecting) {
		int r = 0, c = 0;
		if (cellAt(event->pos(), &r, &c) >= 0) extendSelectionTo(r, c);
		event->accept();
		return;
	}
	QetGraphicsItem::mouseMoveEvent(event);
}

void DiagramTableItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
	if (m_selecting) {
		m_selecting = false;
		event->accept();
		return;
	}
	QetGraphicsItem::mouseReleaseEvent(event);
	// 純點一下(未拖曳)、且表格本來就已選取 → 選取該單一儲存格
	if (m_press_r >= 0 && m_press_was_selected
	    && event->button() == Qt::LeftButton
	    && (event->pos() - m_press_pos).manhattanLength() < 4) {
		int r = 0, c = 0;
		if (cellAt(event->pos(), &r, &c) >= 0)
			setSelectionAnchor(r, c);
	}
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
