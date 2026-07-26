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
#ifndef DRAWINGTABLEPROPERTIESEDITOR_H
#define DRAWINGTABLEPROPERTIESEDITOR_H

#include "../PropertiesEditor/propertieseditorwidget.h"

#include <QList>
#include <QPointer>

class DiagramTableItem;
class QLabel;
class QSpinBox;

/**
	@brief 「繪圖表格」(DiagramTableItem)的屬性面板編輯器。
	提供:選取整列/整欄/整表、對選取儲存格(無選取則整張表)設定或清除
	底色。變更即時套用(經表格自身的 QPropertyUndoCommand,可復原)。
	同時選取多張表時另提供「合併表格」:依畫面位置由上而下串接成一張。
*/
class DrawingTablePropertiesEditor : public PropertiesEditorWidget
{
	Q_OBJECT

	public:
		explicit DrawingTablePropertiesEditor(
			const QList<DiagramTableItem *> &tables,
			QWidget *parent = nullptr);

		QString title() const override;

	private:
		void updateInfo();
		void mergeSelectedTables();

		DiagramTableItem *m_table = nullptr;
		QList<QPointer<DiagramTableItem>> m_tables;
		QLabel *m_info = nullptr;
		QSpinBox *m_size_sb = nullptr;
};

#endif // DRAWINGTABLEPROPERTIESEDITOR_H
