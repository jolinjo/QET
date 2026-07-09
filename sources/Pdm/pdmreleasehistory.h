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
#ifndef PDMRELEASEHISTORY_H
#define PDMRELEASEHISTORY_H

#include <QDate>
#include <QList>
#include <QString>

class QDomDocument;

/**
	@brief .qet 專案級發行史 <pdm-release-history> 的讀寫。
	見 doc/pdm-revision-design.md §5。每個發行版一列,changes 為單一欄位
	(P3.說明 P7.說明)。存於 <project> 直接子 <pdm-release-history>,只在
	發行時由放行者(單寫、於 main、鎖定模型無並發)append 一列。
*/
namespace PdmReleaseHistory
{
	struct Row {
		QString version;      ///< 發行版本(pdm_release_version,如 1.0)
		QString date;         ///< 發行日期(ISO yyyy-MM-dd)
		QString approved_by;  ///< 核准/放行者
		QString changes;      ///< 異動摘要單一字串:P3.說明 P7.說明
	};

	/// 讀全部發行列(依 XML 出現順序,通常即發行先後)
	QList<Row> readReleases(const QDomDocument &doc);

	/// 最近一次發行日期(ISO);無發行史回空字串(= 基準無限早)
	QString lastReleaseDate(const QDomDocument &doc);

	/// 於 <pdm-release-history> 末端 append 一列(容器不存在則建立)
	/// @return true 若成功寫入
	bool appendRelease(QDomDocument &doc, const Row &row);
}

#endif // PDMRELEASEHISTORY_H
