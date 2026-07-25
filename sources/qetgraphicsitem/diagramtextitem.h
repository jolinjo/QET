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
#ifndef DIAGRAM_TEXT_ITEM_H
#define DIAGRAM_TEXT_ITEM_H

#include <QGraphicsTextItem>
#include <QFont>

class Diagram;
class QDomElement;
class QDomDocument;

/**
	This class represents a selectable, movable and editable text field on a
	diagram.
	@see QGraphicsItem::GraphicsItemFlags
*/
class DiagramTextItem : public QGraphicsTextItem
{
	Q_OBJECT

	Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
	Q_PROPERTY(QColor badgeBackground READ badgeBackground WRITE setBadgeBackground)
	Q_PROPERTY(QColor badgeBorder READ badgeBorder WRITE setBadgeBorder)
	Q_PROPERTY(Qt::Alignment alignment READ alignment WRITE setAlignment NOTIFY alignmentChanged)
	Q_PROPERTY(QString plainText READ toPlainText WRITE setPlainText)
	Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged)
	
	
	signals:
		void colorChanged(QColor color);
		void alignmentChanged(Qt::Alignment alignment);
		void textEdited(const QString &old_str, const QString &new_str);
		void fontChanged(QFont font);

	public:
		DiagramTextItem(QGraphicsItem * = nullptr);
		DiagramTextItem(const QString &, QGraphicsItem * = nullptr);

	private:
		void build();
	
	public:
		enum { Type = UserType + 1004 };
		int type() const override { return Type; }

		Diagram *diagram() const;
		virtual void fromXml(const QDomElement &) = 0;
		virtual QDomElement toXml(QDomDocument &) const;
		void edit();

		QPointF mapMovementToScene    (const QPointF &) const;
		QPointF mapMovementFromScene  (const QPointF &) const;
		QPointF mapMovementToParent   (const QPointF &) const;
		QPointF mapMovementFromParent (const QPointF &) const;

		void setFont(const QFont &font);

		void setColor(const QColor& color);
		QColor color() const;

		/// Badge 圓角底色(無效色=無 badge)。畫在文字後方的圓角實心底。
		void setBadgeBackground(const QColor &color);
		QColor badgeBackground() const { return m_badge_bg; }

		/// 文字外框(無效色=無)。圓角外框上色、內部白底(文字黑字)。
		void setBadgeBorder(const QColor &color);
		QColor badgeBorder() const { return m_badge_border; }

		void setNoEditable(bool e = true) {m_no_editable = e;}
		
		void setAlignment(const Qt::Alignment &alignment);
		Qt::Alignment alignment() const;
		bool m_block_alignment = false;
		
		QRectF frameRect() const;
		
		void setHtml(const QString &text);
		void setPlainText(const QString &text);
		bool isHtml() const;

	protected:
		void paint(QPainter *,
			   const QStyleOptionGraphicsItem *,
			   QWidget *) override;
		void focusInEvent(QFocusEvent *) override;
		void focusOutEvent(QFocusEvent *) override;

		void mouseDoubleClickEvent (QGraphicsSceneMouseEvent *event) override;
		void mousePressEvent       (QGraphicsSceneMouseEvent *event) override;
		void mouseMoveEvent        (QGraphicsSceneMouseEvent *event) override;
		void mouseReleaseEvent     (QGraphicsSceneMouseEvent *event) override;

		void hoverEnterEvent(QGraphicsSceneHoverEvent *) override;
		void hoverLeaveEvent(QGraphicsSceneHoverEvent *) override;
		void hoverMoveEvent(QGraphicsSceneHoverEvent *) override;

		virtual void applyRotation(const qreal &);
		void prepareAlignment();
		void finishAlignment();

	
	protected:
		bool
		m_mouse_hover = false,
		m_first_move = true,
		m_no_editable,
		m_is_html = false;

		QString
		m_previous_html_text,
		m_previous_text;
		
		QPointF m_mouse_to_origin_movement;
		QColor m_badge_bg;      ///< badge 圓角底色(無效=無)
		QColor m_badge_border;  ///< 文字外框色(無效=無;白底黑字)

	private:
		QRectF m_alignment_rect;
		Qt::Alignment m_alignment = (Qt::AlignTop | Qt::AlignLeft);
};
#endif
