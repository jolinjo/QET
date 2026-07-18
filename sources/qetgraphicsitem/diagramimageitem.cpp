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
#include "../diagram.h"
#include "../ui/imagepropertieswidget.h"

#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QLineF>

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
					handlerMousePressEvent();
					return true;
				}
				if (event->type() == QEvent::GraphicsSceneMouseMove) {
					handlerMouseMoveEvent(
						static_cast<QGraphicsSceneMouseEvent *>(event));
					return true;
				}
				if (event->type() == QEvent::GraphicsSceneMouseRelease) {
					handlerMouseReleaseEvent();
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
}

/**
	以「中心到游標的距離 / 未縮放半對角線」求新 scale:等比例、繞中心縮放
	(中心 = transformOriginPoint,縮放時於場景中固定不動)。
*/
void DiagramImageItem::handlerMouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
	const QPointF center_scene = mapToScene(boundingRect().center());
	const qreal half_diag = 0.5 * std::hypot(boundingRect().width(),
						 boundingRect().height());
	if (half_diag <= 0) return;
	qreal new_scale =
		QLineF(center_scene, event->scenePos()).length() / half_diag;
	new_scale = qBound(0.05, new_scale, 50.0);
	setScale(new_scale);   // 觸發 ItemScaleHasChanged → adjustHandlerPos
}

void DiagramImageItem::handlerMouseReleaseEvent()
{
	if (diagram() && !qFuzzyCompare(scale(), m_old_scale))
	{
		auto *undo = new QPropertyUndoCommand(this, "scale",
						      m_old_scale, scale());
		undo->setText(tr("Redimensionner %1").arg(name()));
		undo->setAnimated();
		diagram()->undoStack().push(undo);
	}
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
	pixmap_ = pixmap;
	setTransformOriginPoint(boundingRect().center());
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
