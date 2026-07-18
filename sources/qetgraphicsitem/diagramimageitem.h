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
#ifndef DIAGRAM_IMAGE_ITEM_H
#define DIAGRAM_IMAGE_ITEM_H

#include "qetgraphicsitem.h"

#include <QVector>

class QDomElement;
class QDomDocument;
class QetGraphicsHandlerItem;
class QGraphicsSceneMouseEvent;

/**
	This class represents a selectable, movable and editable image on a
	diagram.
	@see QGraphicsItem::GraphicsItemFlags
*/
class DiagramImageItem : public QetGraphicsItem {
	Q_OBJECT

	// constructors, destructor
	public:
	DiagramImageItem(QetGraphicsItem * = nullptr);
	DiagramImageItem(const QPixmap &pixmap, QetGraphicsItem * = nullptr);
	~DiagramImageItem() override;
	
	// attributes
	public:
	enum { Type = UserType + 1007 };
	
	// methods
	public:
	/**
		Enable the use of qgraphicsitem_cast to safely cast a QGraphicsItem into a
		DiagramImageItem
		@return the QGraphicsItem type
	*/
	int type() const override { return Type; }
	
	virtual bool fromXml(const QDomElement &);
	virtual QDomElement toXml(QDomDocument &) const;
	void editProperty() override;
	void setPixmap(const QPixmap &pixmap);
	QRectF boundingRect() const override;
	QString name() const override;
	
	protected:
	void paint(QPainter *, const QStyleOptionGraphicsItem *, QWidget *) override;
	QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
	bool sceneEventFilter(QGraphicsItem *watched, QEvent *event) override;

	private:
	// 選取時於四角顯示控制點,拖曳等比例縮放(改 scale())
	void addHandler();
	void removeHandler();
	void adjustHandlerPos();
	QVector<QPointF> cornerPoints() const;
	void handlerMousePressEvent();
	void handlerMouseMoveEvent(QGraphicsSceneMouseEvent *event);
	void handlerMouseReleaseEvent();

	protected:
	QPixmap pixmap_;

	private:
	QVector<QetGraphicsHandlerItem *> m_handler_vector;
	int m_vector_index = -1;
	qreal m_old_scale = 1;
};
#endif
