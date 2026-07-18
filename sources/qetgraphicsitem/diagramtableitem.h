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
#ifndef DIAGRAM_TABLE_ITEM_H
#define DIAGRAM_TABLE_ITEM_H

#include "qetgraphicsitem.h"

#include <QFont>
#include <QVector>

class QDomElement;
class QDomDocument;
class QetGraphicsHandlerItem;
class QGraphicsSceneMouseEvent;

/**
	@brief 可自由插入的繪圖用表格。
	- 每欄寬度可個別拖曳控制點調整;列高統一(單一設定/拖曳)。
	- 每個儲存格可雙擊輸入文字。
	- 全狀態序列化成單一 "state" property,變更用 QPropertyUndoCommand 復原。
*/
class DiagramTableItem : public QetGraphicsItem
{
	Q_OBJECT
	Q_PROPERTY(QString state READ state WRITE setState)

	public:
		explicit DiagramTableItem(QetGraphicsItem *parent = nullptr);
		~DiagramTableItem() override;

		enum { Type = UserType + 1012 };   // 1010 已被 DynamicElementTextItem 佔用
		int type() const override { return Type; }

		/// 初始化 rows×cols 空表(等寬欄、預設列高)
		void setup(int rows, int cols);

		QString state() const;          ///< 序列化全部狀態(供 undo/存檔)
		void setState(const QString &s);///< 還原全部狀態

		bool fromXml(const QDomElement &e);
		QDomElement toXml(QDomDocument &doc) const;

		QRectF boundingRect() const override;
		QString name() const override { return tr("un tableau"); }

	protected:
		void paint(QPainter *, const QStyleOptionGraphicsItem *, QWidget *) override;
		QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
		bool sceneEventFilter(QGraphicsItem *watched, QEvent *event) override;
		void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;

	private:
		qreal tableWidth() const;
		qreal tableHeight() const;
		qreal columnLeft(int col) const;   ///< 第 col 欄左緣 x
		int cellAt(const QPointF &p, int *row, int *col) const;  ///< 命中儲存格
		void addHandlers();
		void removeHandlers();
		void adjustHandlerPos();
		void editCell(int row, int col);
		void pushStateUndo(const QString &old_state);

		int m_rows = 0;
		int m_cols = 0;
		QVector<qreal> m_col_widths;   ///< 每欄寬度(size = m_cols)
		qreal m_row_height = 24;       ///< 統一列高
		QVector<QString> m_cells;      ///< size = m_rows*m_cols,(r,c)=m_cells[r*cols+c]
		QFont m_font;

		QVector<QetGraphicsHandlerItem *> m_handlers;
		int m_active = -1;             ///< 拖曳中的控制點索引(0..cols-1=欄右緣,cols=列高)
		QString m_drag_old_state;
};

#endif // DIAGRAM_TABLE_ITEM_H
