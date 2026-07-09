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
#ifndef PDMVERSION_H
#define PDMVERSION_H

#include <QString>

class QDomDocument;

/**
	@brief PDM 版本欄位(專案級)的存取。
	見 doc/pdm-revision-design.md §2「版本三拆」。三種版本語意拆開:
	- 工作小版 pdm_work_version:專案級,每次出庫 nextMinor。
	- 發行版 pdm_release_version:專案級,每次發行 nextMajor。
	- 頁修訂索引 indexrev:各頁自有,由使用者維護,PDM 不再覆蓋。
	版本存在 .qet 的 <project> 直接子 <properties> 內,與 QET 專案級
	properties(DiagramContext)相容,QET 存檔會 round-trip 保留。
*/
namespace PdmVersion
{
	/// 專案級 property 名
	extern const char *const WORK_VERSION;      ///< "pdm_work_version"
	extern const char *const RELEASE_VERSION;   ///< "pdm_release_version"

	/// 讀 <project>/<properties>/<property name>;無則回空字串
	QString projectProperty(const QDomDocument &doc, const QString &name);

	/// 寫 <project>/<properties>/<property name>(找不到則建立)。
	/// @return true 若有實際變更
	bool setProjectProperty(QDomDocument &doc, const QString &name,
				const QString &value);

	/// 遷移 fallback:讀舊檔第一個 <diagram indexrev="…"> 的值
	QString legacyFirstDiagramIndexRev(const QDomDocument &doc);

	/// 讀工作小版:優先專案級 WORK_VERSION,無則退回舊檔的首頁 indexrev
	QString workVersion(const QDomDocument &doc);

	QString nextMinor(const QString &current);  ///< 主.次 → 次+1;N → N.1
	QString nextMajor(const QString &current);  ///< 發行:主+1、次歸零
}

#endif // PDMVERSION_H
