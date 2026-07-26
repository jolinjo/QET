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
#include <QUuid>

#include <QColor>
#include <QFont>
#include <QPointF>
#include <QVector>

class QDomElement;
class QDomDocument;
class QetGraphicsHandlerItem;
class QGraphicsSceneMouseEvent;
class QGraphicsProxyWidget;

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

		QUuid uuid() const { return m_uuid; }

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

		/* ── 儲存格選取 + 底色(供屬性面板呼叫)────────────────── */
		bool hasCellSelection() const;          ///< 目前是否有選取儲存格
		int rowCount() const { return m_rows; }
		int columnCount() const { return m_cols; }
		/// 對選取的儲存格上底色(無選取則整張表);空/無效色 = 清除底色
		void setSelectionBackground(const QColor &color);
		void clearSelectionBackground() { setSelectionBackground(QColor()); }
		void selectFullRows();                  ///< 把選取擴成整列(整行)
		void selectFullColumns();               ///< 把選取擴成整欄(整列)
		void selectAllCells();                  ///< 選取整張表
		void clearCellSelection();              ///< 取消儲存格選取
		void selectWholeRow(int row);           ///< 選取整列(第 row 列)
		void selectWholeColumn(int col);        ///< 選取整欄(第 col 欄)
		void addRowAtEnd();                     ///< 表尾新增一列
		void addColumnAtEnd();                  ///< 表右新增一欄
		void deleteSelectedRows();              ///< 刪除選取的列(無選取則末列)
		void deleteSelectedColumns();           ///< 刪除選取的欄(無選取則末欄)
		// 以下皆套用到「選取範圍」(無選取則整張表),與設定底色一致
		void setSelectionAlignH(Qt::Alignment h);  ///< 水平對齊(左/中/右)
		void setSelectionValign(Qt::Alignment v);  ///< 垂直對齊(上/中/下)
		void setSelectionFontSize(int pt);         ///< 文字大小
		int currentFontSize() const;               ///< 首個選取格(或預設)字級
		/// 把多張表合併進本表:依畫面位置(上→下、左→右)把各表的列
		/// 串接起來;欄數取最大、逐格樣式(底色/對齊/字級)保留,
		/// 欄寬取「第一張有該欄的表」;來源表刪除。整包一個復原巨集。
		void mergeWith(QList<DiagramTableItem *> others);
		/* ── Excel 式合併儲存格 ─────────────────────────────── */
		/// 把選取範圍合併成一格(錨點=左上;其餘格資料保留、隱藏,
		/// 取消合併會重現)。選取不足兩格則不動作。
		void mergeSelectedCells();
		/// 取消選取範圍內(相交即算)的所有儲存格合併
		void unmergeSelectedCells();

	signals:
		void tableSelectionChanged();           ///< 儲存格選取有變(更新面板)

	protected:
		void paint(QPainter *, const QStyleOptionGraphicsItem *, QWidget *) override;
		QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
		bool sceneEventFilter(QGraphicsItem *watched, QEvent *event) override;
		void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
		void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
		void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
		void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

	private:
		qreal tableWidth() const;
		qreal tableHeight() const;
		qreal columnLeft(int col) const;   ///< 第 col 欄左緣 x
		int cellAt(const QPointF &p, int *row, int *col) const;  ///< 命中儲存格
		void addHandlers();
		void removeHandlers();
		void adjustHandlerPos();
		void editCell(int row, int col);      ///< 於儲存格上開 inline 編輯框
		void commitEditor();                  ///< 收合 inline 編輯框並寫回文字
		void pushStateUndo(const QString &old_state);
		/// 正規化選取矩形(r0<=r1, c0<=c1);回傳 false 表無選取
		bool selectionRect(int *r0, int *c0, int *r1, int *c1) const;
		void setSelectionAnchor(int row, int col);   ///< 起點(單格)
		void extendSelectionTo(int row, int col);    ///< 拖曳延伸終點
		/// 命中選取列/欄的把手或新增列/欄的「+」;回傳是否已處理該次按下
		bool hitAffordance(const QPointF &p);
		int columnAtX(qreal x) const;                ///< x 落在第幾欄
		void moveRow(int from, int to);              ///< 換列位置
		void moveColumn(int from, int to);           ///< 換欄位置

		int m_rows = 0;
		int m_cols = 0;
		QVector<qreal> m_col_widths;   ///< 每欄寬度(size = m_cols)
		qreal m_row_height = 24;       ///< 統一列高
		QVector<QString> m_cells;      ///< size = m_rows*m_cols,(r,c)=m_cells[r*cols+c]
		QVector<QColor> m_cell_bg;     ///< 每格底色(size = m_rows*m_cols;無效=不填)
		QVector<int> m_cell_halign;    ///< 每格水平對齊 int(Qt::Alignment)
		QVector<int> m_cell_valign;    ///< 每格垂直對齊
		QVector<int> m_cell_size;      ///< 每格字級(0=用 m_font 字級)
		QFont m_font;                  ///< 字型家族 + 預設字級
		/// 合併儲存格區域:QRect(x=欄, y=列, w=跨欄數, h=跨列數)。
		/// 錨點=左上格;被覆蓋格資料保留但不繪製。
		QVector<QRect> m_spans;

		QRect spanAt(int row, int col) const;    ///< 含該格的合併區(無=invalid)
		bool isCoveredCell(int row, int col) const; ///< 在合併區內但非錨點
		QRectF spanCellRect(int row, int col) const; ///< 該格的繪製矩形(含跨距)
		void removeSpansForDeletedRows(int from, int count);
		void removeSpansForDeletedCols(int from, int count);

		// 儲存格選取(矩形範圍;-1 = 無選取)。純顯示狀態,不序列化。
		int m_sel_r0 = -1, m_sel_c0 = -1, m_sel_r1 = -1, m_sel_c1 = -1;
		bool m_selecting = false;      ///< Shift 拖曳範圍選取進行中
		// 拖曳把手換列/欄位置
		enum ReorderKind { NoReorder, ReorderRow, ReorderCol };
		ReorderKind m_reorder = NoReorder;
		int m_reorder_from = -1, m_reorder_to = -1;
		// 按下時暫存,用於「純點一下(未拖曳)= 選單一格」判定
		QPointF m_press_pos;
		int m_press_r = -1, m_press_c = -1;
		bool m_press_was_selected = false;

		QVector<QetGraphicsHandlerItem *> m_handlers;
		int m_active = -1;             ///< 拖曳中的控制點索引(0..cols-1=欄右緣,cols=列高)
		QString m_drag_old_state;
		QGraphicsProxyWidget *m_editor = nullptr;  ///< inline 儲存格編輯框
		int m_edit_index = -1;
		/// 供元件群組等功能識別(序列化於 uuid 屬性)
		QUuid m_uuid = QUuid::createUuid();
};

#endif // DIAGRAM_TABLE_ITEM_H
