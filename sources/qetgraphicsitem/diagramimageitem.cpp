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
#include "diagramimageitem.h"

#include "../PropertiesEditor/propertieseditordialog.h"
#include "../QPropertyUndoCommand/qpropertyundocommand.h"
#include "../QetGraphicsItemModeler/qetgraphicshandleritem.h"
#include "../QetGraphicsItemModeler/qetgraphicshandlerutility.h"
#include "../diagram.h"
#include "../ui/imagepropertieswidget.h"

#include <QGraphicsScene>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QLineF>
#include <QMenu>
#include <QPainterPath>

#include <cmath>

/**
	@brief DiagramImageItem::DiagramImageItem
	Constructor without pixmap
	@param parent_item the parent graphics item
*/
DiagramImageItem::DiagramImageItem(QetGraphicsItem *parent_item):
	QetGraphicsItem(parent_item)
{
	setFlags(QGraphicsItem::ItemIsSelectable|QGraphicsItem::ItemIsMovable|QGraphicsItem::ItemSendsGeometryChanges);
}

/**
	@brief DiagramImageItem::DiagramImageItem
	Constructor with pixmap
	@param pixmap the pixmap to be draw
	@param parent_item the parent graphic item
*/
DiagramImageItem::DiagramImageItem(const QPixmap &pixmap, QetGraphicsItem *parent_item):
	QetGraphicsItem(parent_item),
	pixmap_(pixmap)
{
	setTransformOriginPoint(boundingRect().center());
	setFlags(QGraphicsItem::ItemIsSelectable|QGraphicsItem::ItemIsMovable|QGraphicsItem::ItemSendsGeometryChanges);
}

/**
	@brief DiagramImageItem::~DiagramImageItem
	Destructor
*/
DiagramImageItem::~DiagramImageItem()
{
	removeHandler();
}

/**
	四角控制點的位置(item 座標)。只用四角 → 縮放恆等比例。
*/
QVector<QPointF> DiagramImageItem::cornerPoints() const
{
	const QRectF r = boundingRect();
	return { r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft() };
}

/**
	選取時建立四角控制點(藍色方塊),裝上 sceneEventFilter 以攔截拖曳。
*/
void DiagramImageItem::addHandler()
{
	if (m_crop_mode) return;
	if (m_handler_vector.isEmpty() && scene())
	{
		m_handler_vector = QetGraphicsHandlerItem::handlerForPoint(
			mapToScene(cornerPoints()));
		for (QetGraphicsHandlerItem *h : qAsConst(m_handler_vector))
		{
			h->setZValue(this->zValue() + 1);
			h->setColor(Qt::blue);
			scene()->addItem(h);
			h->installSceneEventFilter(this);
		}
	}
}

void DiagramImageItem::removeHandler()
{
	if (!m_handler_vector.isEmpty())
	{
		qDeleteAll(m_handler_vector);
		m_handler_vector.clear();
	}
}

/**
	圖片移動/縮放/旋轉後,把控制點移到目前四角(scene 座標)。
*/
void DiagramImageItem::adjustHandlerPos()
{
	if (m_handler_vector.isEmpty()) return;
	if (m_crop_mode) { adjustCropHandlerPos(); return; }
	const QPolygonF scene_pts = mapToScene(cornerPoints());
	if (m_handler_vector.size() == scene_pts.size())
		for (int i = 0; i < m_handler_vector.size(); ++i)
			m_handler_vector.at(i)->setPos(scene_pts.at(i));
}

QVariant DiagramImageItem::itemChange(GraphicsItemChange change,
				      const QVariant &value)
{
	if (change == ItemSelectedHasChanged)
	{
		if (value.toBool() && scene())
			addHandler();
		else if (m_crop_mode)
			cancelCrop();
		else
			removeHandler();
	}
	else if (change == ItemPositionHasChanged
		 || change == ItemScaleHasChanged
		 || change == ItemRotationHasChanged
		 || change == ItemTransformHasChanged)
	{
		adjustHandlerPos();
	}
	return QetGraphicsItem::itemChange(change, value);
}

/**
	攔截控制點的滑鼠事件(press/move/release)分派到縮放處理。
*/
bool DiagramImageItem::sceneEventFilter(QGraphicsItem *watched, QEvent *event)
{
	if (watched->type() == QetGraphicsHandlerItem::Type)
	{
		QetGraphicsHandlerItem *qghi =
			qgraphicsitem_cast<QetGraphicsHandlerItem *>(watched);
		if (m_handler_vector.contains(qghi))
		{
			m_vector_index = m_handler_vector.indexOf(qghi);
			if (m_vector_index != -1)
			{
				if (event->type() == QEvent::GraphicsSceneMousePress) {
					if (!m_crop_mode) handlerMousePressEvent();
					return true;
				}
				if (event->type() == QEvent::GraphicsSceneMouseMove) {
					if (m_crop_mode)
						handlerCropMoveEvent(
							static_cast<QGraphicsSceneMouseEvent *>(event));
					else
						handlerMouseMoveEvent(
							static_cast<QGraphicsSceneMouseEvent *>(event));
					return true;
				}
				if (event->type() == QEvent::GraphicsSceneMouseRelease) {
					if (!m_crop_mode) handlerMouseReleaseEvent();
					return true;
				}
			}
		}
	}
	return false;
}

void DiagramImageItem::handlerMousePressEvent()
{
	m_old_scale = scale();
	m_old_pos = pos();
	// 對角控制點作為錨點(拖某角時對角固定不動),記住其固定場景座標
	const QVector<QPointF> c = cornerPoints();   // 0 TL,1 TR,2 BR,3 BL
	if (m_vector_index >= 0 && m_vector_index < c.size()) {
		m_resize_anchor_item = c.at((m_vector_index + 2) % 4);
		m_resize_anchor_scene = mapToScene(m_resize_anchor_item);
	}
}

/**
	以「對角錨點到游標的距離 / 未縮放對角線」求新 scale(等比例),縮放後把
	對角錨點補回原場景位置 → 拖角時對角控制點固定不動,符合一般操作直覺。
*/
void DiagramImageItem::handlerMouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
	const qreal diag = std::hypot(boundingRect().width(),
				      boundingRect().height());
	if (diag <= 0) return;
	qreal new_scale =
		QLineF(m_resize_anchor_scene, event->scenePos()).length() / diag;
	new_scale = qBound(0.05, new_scale, 50.0);
	setScale(new_scale);
	// 補償位置,讓對角錨點停在原場景座標(setPos 用基底類別,避免對齊格線抖動)
	const QPointF now = mapToScene(m_resize_anchor_item);
	QGraphicsObject::setPos(pos() + (m_resize_anchor_scene - now));
}

void DiagramImageItem::handlerMouseReleaseEvent()
{
	if (diagram()
	    && (!qFuzzyCompare(scale(), m_old_scale) || pos() != m_old_pos))
	{
		// 縮放同時動到 scale 與 pos:用巨集一起記錄以正確復原
		diagram()->undoStack().beginMacro(
			tr("Redimensionner %1").arg(name()));
		diagram()->undoStack().push(new QPropertyUndoCommand(
			this, "scale", m_old_scale, scale()));
		diagram()->undoStack().push(new QPropertyUndoCommand(
			this, "pos", QVariant(m_old_pos), QVariant(pos())));
		diagram()->undoStack().endMacro();
	}
}

/* ── 剪裁模式(在圖片上直接拉框,帶控制點)───────────────────────── */

void DiagramImageItem::startCrop()
{
	if (m_crop_mode || pixmap_.isNull()) return;
	setSelected(true);
	removeHandler();                 // 收起等比例縮放控制點
	m_crop_mode = true;
	m_crop_rect = boundingRect();    // 初始剪裁框 = 整張圖
	setFlag(ItemIsMovable, false);   // 剪裁時不移動圖片
	setFlag(ItemIsFocusable, true);
	setFocus();                      // 接收 Enter/Esc
	addCropHandlers();
	update();
}

void DiagramImageItem::addCropHandlers()
{
	if (!scene()) return;
	m_handler_vector = QetGraphicsHandlerItem::handlerForPoint(
		mapToScene(QetGraphicsHandlerUtility::pointsForRect(m_crop_rect)));
	for (QetGraphicsHandlerItem *h : qAsConst(m_handler_vector))
	{
		h->setZValue(this->zValue() + 1);
		h->setColor(Qt::red);
		scene()->addItem(h);
		h->installSceneEventFilter(this);
	}
}

void DiagramImageItem::adjustCropHandlerPos()
{
	const QPolygonF pts = mapToScene(
		QetGraphicsHandlerUtility::pointsForRect(m_crop_rect));
	if (m_handler_vector.size() == pts.size())
		for (int i = 0; i < m_handler_vector.size(); ++i)
			m_handler_vector.at(i)->setPos(pts.at(i));
}

void DiagramImageItem::handlerCropMoveEvent(QGraphicsSceneMouseEvent *event)
{
	const QPointF p = mapFromScene(event->scenePos());
	QRectF r = QetGraphicsHandlerUtility::rectForPosAtIndex(
			m_crop_rect, p, m_vector_index)
		.intersected(boundingRect());
	if (r.width() >= 4 && r.height() >= 4)
	{
		m_crop_rect = r;
		adjustCropHandlerPos();
		update();
	}
}

void DiagramImageItem::applyCrop()
{
	if (!m_crop_mode) return;
	const QRect r = m_crop_rect.toRect().intersected(pixmap_.rect());
	m_crop_mode = false;
	removeHandler();
	setFlag(ItemIsMovable, true);

	if (r.width() >= 2 && r.height() >= 2 && r != pixmap_.rect())
	{
		// 保留被裁區域在畫面上的位置:算出裁後要補的位移(平移不受縮放/旋轉影響)
		const QPointF anchor_scene = mapToScene(m_crop_rect.topLeft());
		const QPixmap old_pix = pixmap_;
		const QPointF old_pos = pos();
		const QPixmap cropped = pixmap_.copy(r);

		setPixmap(cropped);   // 暫時套用以取得裁後原點對應的場景座標
		const QPointF new_pos =
			old_pos + (anchor_scene - mapToScene(QPointF(0, 0)));
		setPixmap(old_pix);   // 還原,實際變更交給 undo(巨集依序:pixmap→pos)
		setPos(old_pos);

		if (diagram())
		{
			diagram()->undoStack().beginMacro(tr("剪裁圖片"));
			diagram()->undoStack().push(new QPropertyUndoCommand(
				this, "pixmap", QVariant(old_pix), QVariant(cropped)));
			diagram()->undoStack().push(new QPropertyUndoCommand(
				this, "pos", QVariant(old_pos), QVariant(new_pos)));
			diagram()->undoStack().endMacro();
		}
		else
		{
			setPixmap(cropped);
			setPos(new_pos);
		}
	}

	if (isSelected()) addHandler();   // 回到等比例縮放控制點
	update();
}

void DiagramImageItem::cancelCrop()
{
	if (!m_crop_mode) return;
	m_crop_mode = false;
	removeHandler();
	setFlag(ItemIsMovable, true);
	if (isSelected()) addHandler();
	update();
}

void DiagramImageItem::contextMenuEvent(QGraphicsSceneContextMenuEvent *event)
{
	if (diagram() && diagram()->isReadOnly()) { event->ignore(); return; }
	QMenu menu;
	if (!m_crop_mode) {
		menu.addAction(tr("剪裁圖片"), this, [this] { startCrop(); });
	} else {
		menu.addAction(tr("套用剪裁 (雙擊/Enter)"),
			       this, [this] { applyCrop(); });
		menu.addAction(tr("取消剪裁 (Esc)"), this, [this] { cancelCrop(); });
	}
	event->accept();
	menu.exec(event->screenPos());
}

/**
	剪裁模式:雙擊圖片即套用剪裁;否則沿用預設(開啟屬性)。
*/
void DiagramImageItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
	if (m_crop_mode) {
		applyCrop();
		event->accept();
		return;
	}
	QetGraphicsItem::mouseDoubleClickEvent(event);
}

/**
	剪裁模式:Enter 套用、Esc 取消。
*/
void DiagramImageItem::keyPressEvent(QKeyEvent *event)
{
	if (m_crop_mode) {
		if (event->key() == Qt::Key_Return
		    || event->key() == Qt::Key_Enter) {
			applyCrop();
			event->accept();
			return;
		}
		if (event->key() == Qt::Key_Escape) {
			cancelCrop();
			event->accept();
			return;
		}
	}
	QetGraphicsItem::keyPressEvent(event);
}

/**
	@brief DiagramImageItem::paint
	Draw the pixmap.
	@param painter the Qpainter to use for draw the pixmap
	@param option the style option
	@param widget the QWidget where we draw the pixmap
*/
void DiagramImageItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
	painter -> drawPixmap(pixmap_.rect(),pixmap_);

	Q_UNUSED(option); Q_UNUSED(widget);

	if (m_crop_mode) {
		// 剪裁框外變暗、框線以紅色虛線標示
		painter->save();
		QPainterPath path;
		path.addRect(boundingRect());
		path.addRect(m_crop_rect);
		painter->fillPath(path, QColor(0, 0, 0, 110));
		QPen cp(Qt::red);
		cp.setStyle(Qt::DashLine);
		cp.setCosmetic(true);
		painter->setPen(cp);
		painter->setBrush(Qt::NoBrush);
		painter->drawRect(m_crop_rect);
		painter->restore();
		return;
	}

	if (isSelected()) {
		painter -> save();
		// Annulation des renderhints
		painter -> setRenderHint(QPainter::Antialiasing,          false);
		painter -> setRenderHint(QPainter::TextAntialiasing,      false);
		painter -> setRenderHint(QPainter::SmoothPixmapTransform, false);
		// Dessin du cadre de selection en noir à partir du boundingrect
		QPen t(Qt::black);
		t.setStyle(Qt::DashLine);
		painter -> setPen(t);
		painter -> drawRect(boundingRect());
		painter -> restore();
	}
}

/**
	@brief DiagramImageItem::editProperty
	Open the appropriate dialog to edit this image
*/
void DiagramImageItem::editProperty()
{
	if (diagram() -> isReadOnly()) return;
	PropertiesEditorDialog dialog(new ImagePropertiesWidget(this), QApplication::activeWindow());
	dialog.exec();
}

/**
	@brief DiagramImageItem::setPixmap
	Set the new pixmap to be draw
	@param pixmap the new pixmap
*/
void DiagramImageItem::setPixmap(const QPixmap &pixmap) {
	prepareGeometryChange();
	pixmap_ = pixmap;
	setTransformOriginPoint(boundingRect().center());
	adjustHandlerPos();   // 尺寸變了(如剪裁),控制點跟著移到新四角
	update();
}

/**
	@brief DiagramImageItem::boundingRect
	the outer bounds of the item as a rectangle,
	if no pixmap are set, return a default QRectF
	@return a QRectF represent the bounding rectangle
*/
QRectF DiagramImageItem::boundingRect() const
{
	if (!pixmap_.isNull()) {
		return (QRectF(pixmap_.rect()));
	} else {
		QRectF bound;
		return (bound);
	}
}

/**
	@brief DiagramImageItem::name
	@return the generic name of this item (picture)
*/
QString DiagramImageItem::name() const
{
	return tr("une image");
}

/**
	@brief DiagramImageItem::fromXml
	Load this image from xml element e
	@param e
	@return true if successfully loaded.
*/
bool DiagramImageItem::fromXml(const QDomElement &e)
{
	if (e.tagName() != "image") {
		return (false);
	}
	
	QDomNode image_node = e.firstChild();
	if (!image_node.isText()) {
		return (false);
	}

	//load xml image to QByteArray
	QByteArray array;
	array = QByteArray::fromBase64(e.text().toLatin1());

	//Set QPixmap from the array
	QPixmap pixmap;
	pixmap.loadFromData(array);
	setPixmap(pixmap);

	setScale(e.attribute("size").toDouble());
	setRotation(e.attribute("rotation").toDouble());
		//We directly call setPos from QGraphicsObject, because QetGraphicsItem will snap to grid
	QGraphicsObject::setPos(e.attribute("x").toDouble(), e.attribute("y").toDouble());
	setZValue(e.attribute("z", QString::number(this->zValue())).toDouble());
	is_movable_ = (e.attribute("is_movable").toInt());

	return (true);
}

/**
	@param document Le document XML a utiliser
	@return L'element XML representant l'image
*/
QDomElement DiagramImageItem::toXml(QDomDocument &document) const
{
	QDomElement result = document.createElement("image");
	//write some attribute
	result.setAttribute("x", QString::number(pos().x()));
	result.setAttribute("y", QString::number(pos().y()));
	result.setAttribute("z", QString::number(this->zValue()));
	result.setAttribute("rotation", QString::number(QET::correctAngle(rotation())));
	result.setAttribute("size", QString::number(scale()));
	result.setAttribute("is_movable", bool(is_movable_));

	//write the pixmap in the xml element after he was been transformed to base64
	QByteArray array;
	QBuffer buffer(&array);
	buffer.open(QIODevice::ReadWrite);
	pixmap_.save(&buffer, "PNG");
	QDomText base64 = document.createTextNode(array.toBase64());
	result.appendChild(base64);

	return(result);
}
