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
#ifndef SELECTIONFLOATINGTOOLBAR_H
#define SELECTIONFLOATINGTOOLBAR_H

#include <QColor>
#include <QFrame>
#include <QPointer>

class DiagramTextItem;
class QGraphicsView;
class QHBoxLayout;
class QToolButton;

/**
	@brief 選取項目後浮在畫布上方的編輯工具列(ClickUp 風)。
	目前支援「文字項目」:粗體/底線、文字色票、圓角底色(badge)色票。
	是 QGraphicsView viewport 的子視窗,依綁定項目在場景中的位置定位。
*/
class SelectionFloatingToolbar : public QFrame
{
	Q_OBJECT

	public:
		explicit SelectionFloatingToolbar(QGraphicsView *view);

		/// 綁定一個文字項目並顯示工具列(nullptr=隱藏)
		void showForText(DiagramTextItem *text);
		/// 依綁定項目在場景的位置重新定位;無綁定則隱藏
		void reposition();

	private:
		void clearContent();
		QToolButton *addTextButton(const QString &text, const QString &tip);
		void addSwatch(const QColor &c, bool badge);
		void addSeparator();
		void toggleBold();
		void toggleUnderline();
		void applyColor(const QColor &c);
		void applyBadge(const QColor &c);   ///< 無效色=移除

		QGraphicsView *m_view = nullptr;
		QPointer<DiagramTextItem> m_text;
		QHBoxLayout *m_layout = nullptr;
};

#endif // SELECTIONFLOATINGTOOLBAR_H
