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
#include "drawingtablepropertieseditor.h"

#include "../qetgraphicsitem/diagramtableitem.h"

#include <QColor>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

DrawingTablePropertiesEditor::DrawingTablePropertiesEditor(
	const QList<DiagramTableItem *> &tables, QWidget *parent) :
	PropertiesEditorWidget(parent)
{
	for (DiagramTableItem *t : tables)
		if (t) m_tables << t;
	// 依畫面位置排序,首張(最上/最左)為主表
	std::sort(m_tables.begin(), m_tables.end(),
		  [](const QPointer<DiagramTableItem> &a,
		     const QPointer<DiagramTableItem> &b) {
		const QPointF pa = a->scenePos(), pb = b->scenePos();
		if (!qFuzzyCompare(pa.y(), pb.y())) return pa.y() < pb.y();
		return pa.x() < pb.x();
	});
	m_table = m_tables.isEmpty() ? nullptr : m_tables.first().data();

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);

	auto *box = new QGroupBox(tr("表格"), this);
	auto *v = new QVBoxLayout(box);

	m_info = new QLabel(box);
	m_info->setWordWrap(true);
	v->addWidget(m_info);

	// 多選表格:提供合併(依畫面位置上→下串接成一張)
	if (m_tables.count() >= 2) {
		auto *merge = new QPushButton(
			tr("合併 %1 張表格").arg(m_tables.count()), box);
		merge->setToolTip(tr("依畫面位置由上而下,把選取的表格"
				     "串接成一張(可復原)"));
		v->addWidget(merge);
		connect(merge, &QPushButton::clicked, this,
			&DrawingTablePropertiesEditor::mergeSelectedTables);
	}

	auto *sel_row = new QHBoxLayout();
	auto *btn_row = new QPushButton(tr("選取整列"), box);
	auto *btn_col = new QPushButton(tr("選取整欄"), box);
	auto *btn_all = new QPushButton(tr("選取整表"), box);
	sel_row->addWidget(btn_row);
	sel_row->addWidget(btn_col);
	sel_row->addWidget(btn_all);
	v->addLayout(sel_row);

	// Excel 式合併儲存格:選取範圍合併成一格 / 取消合併
	auto *span_row = new QHBoxLayout();
	auto *btn_merge_cells = new QPushButton(tr("合併儲存格"), box);
	btn_merge_cells->setToolTip(tr("把選取的範圍合併成一格"
				       "(錨點=左上;其餘格內容保留、"
				       "取消合併會重現)"));
	auto *btn_unmerge = new QPushButton(tr("取消合併"), box);
	btn_unmerge->setToolTip(tr("拆開選取範圍內的合併儲存格"));
	span_row->addWidget(btn_merge_cells);
	span_row->addWidget(btn_unmerge);
	v->addLayout(span_row);
	connect(btn_merge_cells, &QPushButton::clicked, this, [this]() {
		if (m_table) m_table->mergeSelectedCells();
	});
	connect(btn_unmerge, &QPushButton::clicked, this, [this]() {
		if (m_table) m_table->unmergeSelectedCells();
	});

	// 對齊(水平:左/中/右;垂直:上/中/下)
	v->addWidget(new QLabel(tr("對齊"), box));
	auto *align_row = new QHBoxLayout();
	align_row->setSpacing(3);
	auto *ah_l = new QToolButton(box); ah_l->setText(tr("左"));
	auto *ah_c = new QToolButton(box); ah_c->setText(tr("中"));
	auto *ah_r = new QToolButton(box); ah_r->setText(tr("右"));
	auto *av_t = new QToolButton(box); av_t->setText(tr("上"));
	av_t->setToolTip(tr("靠上"));
	auto *av_m = new QToolButton(box); av_m->setText(tr("中"));
	av_m->setToolTip(tr("垂直置中"));
	auto *av_b = new QToolButton(box); av_b->setText(tr("下"));
	av_b->setToolTip(tr("靠下"));
	for (QToolButton *b : {ah_l, ah_c, ah_r, av_t, av_m, av_b})
		b->setFixedSize(30, 24);
	align_row->addWidget(ah_l);
	align_row->addWidget(ah_c);
	align_row->addWidget(ah_r);
	align_row->addSpacing(10);
	align_row->addWidget(av_t);
	align_row->addWidget(av_m);
	align_row->addWidget(av_b);
	align_row->addStretch();
	v->addLayout(align_row);
	connect(ah_l, &QToolButton::clicked, this, [this]() {
		if (m_table) m_table->setSelectionAlignH(Qt::AlignLeft); });
	connect(ah_c, &QToolButton::clicked, this, [this]() {
		if (m_table) m_table->setSelectionAlignH(Qt::AlignHCenter); });
	connect(ah_r, &QToolButton::clicked, this, [this]() {
		if (m_table) m_table->setSelectionAlignH(Qt::AlignRight); });
	connect(av_t, &QToolButton::clicked, this, [this]() {
		if (m_table) m_table->setSelectionValign(Qt::AlignTop); });
	connect(av_m, &QToolButton::clicked, this, [this]() {
		if (m_table) m_table->setSelectionValign(Qt::AlignVCenter); });
	connect(av_b, &QToolButton::clicked, this, [this]() {
		if (m_table) m_table->setSelectionValign(Qt::AlignBottom); });

	// 文字大小(套用到選取範圍)
	auto *size_row = new QHBoxLayout();
	size_row->addWidget(new QLabel(tr("文字大小"), box));
	m_size_sb = new QSpinBox(box);
	m_size_sb->setRange(4, 200);
	m_size_sb->setValue(m_table ? m_table->currentFontSize() : 9);
	size_row->addWidget(m_size_sb);
	size_row->addStretch();
	v->addLayout(size_row);
	connect(m_size_sb, QOverload<int>::of(&QSpinBox::valueChanged), this,
		[this](int val) { if (m_table) m_table->setSelectionFontSize(val); });

	// 底色:ClickUp 風色票,直接點選套用到選取範圍(不開調色盤)
	v->addWidget(new QLabel(tr("底色"), box));
	static const char *const BG_COLORS[] = {
		"#d63d3d", "#e8710a", "#e0a800", "#2f6fdb", "#4a3fc7",
		"#d63384", "#2f8f5b", "#9b9a97",
		"#fbe4e4", "#faebdd", "#fbf3db", "#ddedea", "#ddebf1",
		"#eae4f2", "#f4dfeb", "#e3e2e0" };
	auto *grid = new QGridLayout();
	grid->setSpacing(3);
	const int cols = 8;
	int n = 0;
	for (const char *hex : BG_COLORS) {
		const QColor c(QString::fromLatin1(hex));
		auto *b = new QToolButton(box);
		b->setFixedSize(20, 20);
		b->setCursor(Qt::PointingHandCursor);
		b->setToolTip(c.name());
		b->setStyleSheet(QStringLiteral(
			"QToolButton{border:1px solid #c8c8c8;border-radius:3px;"
			"background:%1;}").arg(c.name()));
		connect(b, &QToolButton::clicked, this, [this, c]() {
			if (m_table) m_table->setSelectionBackground(c);
		});
		grid->addWidget(b, n / cols, n % cols);
		++n;
	}
	// ⊘ 清除底色
	auto *none = new QToolButton(box);
	none->setFixedSize(20, 20);
	none->setText(QStringLiteral("⊘"));
	none->setToolTip(tr("清除底色"));
	none->setCursor(Qt::PointingHandCursor);
	none->setStyleSheet(QStringLiteral(
		"QToolButton{border:1px solid #c8c8c8;border-radius:3px;"
		"background:white;}"));
	connect(none, &QToolButton::clicked, this, [this]() {
		if (m_table) m_table->clearSelectionBackground();
	});
	grid->addWidget(none, n / cols, n % cols);
	auto *grid_wrap = new QHBoxLayout();
	grid_wrap->addLayout(grid);
	grid_wrap->addStretch();
	v->addLayout(grid_wrap);

	root->addWidget(box);
	root->addStretch();

	connect(btn_row, &QPushButton::clicked, this, [this]() {
		if (m_table) m_table->selectFullRows();
	});
	connect(btn_col, &QPushButton::clicked, this, [this]() {
		if (m_table) m_table->selectFullColumns();
	});
	connect(btn_all, &QPushButton::clicked, this, [this]() {
		if (m_table) m_table->selectAllCells();
	});

	if (m_table) {
		connect(m_table, &DiagramTableItem::tableSelectionChanged,
			this, &DrawingTablePropertiesEditor::updateInfo);
		connect(m_table, &QObject::destroyed, this, [this]() {
			m_table = nullptr;
			updateInfo();
		});
	}
	updateInfo();
}

QString DrawingTablePropertiesEditor::title() const
{
	return tr("表格");
}

void DrawingTablePropertiesEditor::updateInfo()
{
	// 選取變動時,字級輸入框反映首個選取格的字級(不觸發套用)
	if (m_size_sb && m_table) {
		m_size_sb->blockSignals(true);
		m_size_sb->setValue(m_table->currentFontSize());
		m_size_sb->blockSignals(false);
	}
	if (!m_info) return;
	if (!m_table) { m_info->setText(QStringLiteral("—")); return; }
	if (m_tables.count() >= 2) {
		m_info->setText(tr(
			"已選取 %1 張表格。「合併」會依畫面位置由上而下\n"
			"串接成一張;下方設定只作用於第一張表。")
				.arg(m_tables.count()));
		return;
	}
	if (m_table->hasCellSelection())
		m_info->setText(tr(
			"已選取儲存格:設定底色會套用到選取範圍。\n"
			"可先按「選取整列/整欄」擴大範圍。"));
	else
		m_info->setText(tr(
			"未選取儲存格:設定底色會套用到整張表。\n"
			"點一格=選單格;Shift+拖曳=選範圍。"));
}

void DrawingTablePropertiesEditor::mergeSelectedTables()
{
	QList<DiagramTableItem *> list;
	for (const QPointer<DiagramTableItem> &p : m_tables)
		if (p) list << p.data();
	if (list.count() < 2) return;
	DiagramTableItem *target = list.takeFirst();   // 已排序:最上/最左
	target->mergeWith(list);
	// 來源表已自場景移除,選取狀態隨之更新,面板會重建成單表模式
	m_tables.clear();
	m_tables << target;
	updateInfo();
}
