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
#ifndef DCCCODES_H
#define DCCCODES_H

#include <QComboBox>
#include <QString>
#include <QVector>

/**
	@brief IEC 61355-1(DCC)文件分類代碼清單:單一真相來源。
	供「圖框屬性」與「PDM 新增頁面」的『文件類別』下拉共用。
	每項附說明/應用情境(以 tooltip 呈現),依領域(電控 E / 機械 M /
	管理 B-C-D)分組,組間插分隔線。
	維護規範原文見公司文件分類規範(基於 IEC 61355-1)。
*/
namespace DccCodes {

	struct Entry {
		QString section;   ///< 群組(僅用來在組界插分隔線)
		QString label;     ///< 下拉顯示文字(含 DCC 代碼)
		QString tip;       ///< 說明 + 應用情境(tooltip)
	};

	inline QVector<Entry> entries()
	{
		const QString E = QStringLiteral("E");
		const QString M = QStringLiteral("M");
		const QString B = QStringLiteral("B");
		return {
			// 一、電控與自動化 (E-Electrotechnical)
			{E, QStringLiteral("&EFA 概觀圖 / 單線圖"),
			 QStringLiteral("系統架構與迴路。應用:設備整體通訊網路拓撲圖"
				"(如 EtherCAT 主從架構)、大功率三相電源供電系統架構圖。")},
			{E, QStringLiteral("&EFS 詳細電路圖"),
			 QStringLiteral("電控系統核心控制電路圖:主迴路電源分配、PLC I/O "
				"模組、伺服/步進驅動器、安全迴路、現場感測器接線。")},
			{E, QStringLiteral("&EPB 電氣零件清單"),
			 QStringLiteral("自動從繪圖軟體導出的電控物料清單:斷路器、繼電器、"
				"PLC 模組的型號、製造商與採購料號。")},
			{E, QStringLiteral("&EMA 端子排 / 插頭接線圖"),
			 QStringLiteral("詳細記錄 -X1、-X2 等中繼端子排或重載連接器的針腳"
				"定義,為現場配線與除錯的核心圖表。")},
			{E, QStringLiteral("&EMB 線纜清單 / 接線表"),
			 QStringLiteral("電控櫃連接至外部機構件(馬達、氣壓閥島)的多芯電纜"
				"對照表,標明線徑、長度與兩端去向。")},
			{E, QStringLiteral("&ELD 電氣佈置圖"),
			 QStringLiteral("電控箱內部元件實體配置圖、走線槽規劃圖,以及盤門"
				"按鈕與人機介面(HMI)的開孔開關配置圖。")},
			{E, QStringLiteral("&ECC 電氣技術規格書"),
			 QStringLiteral("定義電控櫃防護等級(如 IP54)、額定供電電壓、"
				"最大短路電流等基本硬體規範。")},
			{E, QStringLiteral("&ECT 電氣測試規範"),
			 QStringLiteral("設備出廠前(FAT)電控箱的耐壓、絕緣測試紀錄表與 "
				"I/O 點位功能點檢表。")},
			{E, QStringLiteral("&EDA 邏輯 / 軟體功能圖"),
			 QStringLiteral("PLC 程式邏輯狀態機(State Machine)流程圖、"
				"安全 PLC 控制邏輯圖。")},
			{E, QStringLiteral("&EDB 訊號列表 / I/O 表"),
			 QStringLiteral("PLC I/O 對照表、HMI 畫面變數(Tag)、SCADA 通訊"
				"定址與 Modbus/EtherCAT 對照表。")},

			// 二、機械與機構 (M-Mechanical)
			{M, QStringLiteral("&MFS 流體功能迴路圖"),
			 QStringLiteral("氣壓迴路圖(Pneumatic)或油壓迴路圖:三點組合、"
				"氣壓閥島、氣缸與真空吸嘴的動作邏輯與管路接法。")},
			{M, QStringLiteral("&MPB 機械零件清單"),
			 QStringLiteral("機械機構的零件與標準件 BOM:滑軌、螺桿、氣缸、"
				"軸承、馬達減速機等採購清單。")},
			{M, QStringLiteral("&MTD 機械尺寸 / 外形圖"),
			 QStringLiteral("機台整機的外形尺寸圖、總裝配圖,供客戶與現場確認"
				"設備佔地與廠房擺放空間。")},
			{M, QStringLiteral("&MTE 製造加工 / 爆炸圖"),
			 QStringLiteral("機構零件的加工製造圖,或指導現場裝配、後續客戶"
				"維修使用的立體爆炸分解圖。")},

			// 三、專案管理 / 品質 / 維運 (B-C-D)
			{B, QStringLiteral("&CBA 專案建議書 / 報價單"),
			 QStringLiteral("專案初期針對客戶需求開立的技術規格建議書與成本"
				"價格估算單。")},
			{B, QStringLiteral("&BBA 會議記錄"),
			 QStringLiteral("設計審查會(Design Review)、專案啟動會"
				"(Kick-off)等核心會議決議與待辦事項追蹤。")},
			{B, QStringLiteral("&BBE 專案進度表"),
			 QStringLiteral("專案甘特圖、ClickUp 任務時程表、機台產線排程計畫。")},
			{B, QStringLiteral("&QCC 品質保證 / 出廠證書"),
			 QStringLiteral("設備符合 CE、UL 或客戶指定 IEC 標準的出廠檢驗與"
				"品質合格證書。")},
			{B, QStringLiteral("&DAA 產品資料表"),
			 QStringLiteral("整合外購大廠組件(變頻器、安全繼電器、感測器)的"
				"原廠技術規格書附錄。")},
			{B, QStringLiteral("&DFA 操作說明書"),
			 QStringLiteral("交付客戶的操作人員手冊:機台基本操作流程、人機"
				"畫面切換與常見故障排除指南。")},
			{B, QStringLiteral("&DFB 維護保養手冊"),
			 QStringLiteral("耗材清單與定期保養指南:滑軌注脂、氣壓濾器排水、"
				"電控櫃濾網更換週期。")},
		};
	}

	/// 把 DCC 清單填入下拉:逐項加入、設 tooltip(說明/情境)、組間插分隔線。
	inline void populate(QComboBox *cb)
	{
		QString last_section;
		for (const Entry &e : entries()) {
			if (!last_section.isEmpty() && e.section != last_section
			    && cb->count() > 0)
				cb->insertSeparator(cb->count());
			last_section = e.section;
			cb->addItem(e.label);
			cb->setItemData(cb->count() - 1, e.tip, Qt::ToolTipRole);
		}
	}
}

#endif // DCCCODES_H
