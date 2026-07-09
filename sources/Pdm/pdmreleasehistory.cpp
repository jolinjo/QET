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
#include "pdmreleasehistory.h"

#include "pdmversion.h"

#include <QDomDocument>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

/*
	重要:發行史必須存成 <project>/<properties> 裡的一個 <property>,
	不能用自訂的 <pdm-release-history> 子元素。因為 QETProject::toXml()
	完全從資料模型重建 XML、不保留未知節點——使用者在 QET 開檔存檔一次,
	自訂元素就會被丟掉。而 <property> 走 DiagramContext round-trip,QET
	會保留(pdm_work_version 亦同)。故整份發行史序列化成 JSON 存進單一
	property "pdm_release_history"。見 doc/pdm-revision-design.md §5.1。
*/
namespace
{
	const char *const HISTORY_PROP = "pdm_release_history";
}

QList<PdmReleaseHistory::Row> PdmReleaseHistory::readReleases(
	const QDomDocument &doc)
{
	QList<Row> rows;
	const QString json = PdmVersion::projectProperty(doc,
		QLatin1String(HISTORY_PROP));
	if (json.isEmpty()) return rows;
	const QJsonDocument jd = QJsonDocument::fromJson(json.toUtf8());
	if (!jd.isArray()) return rows;
	const QJsonArray arr = jd.array();
	for (const QJsonValue &v : arr) {
		const QJsonObject o = v.toObject();
		Row row;
		row.version     = o.value(QStringLiteral("version")).toString();
		row.date        = o.value(QStringLiteral("date")).toString();
		row.approved_by = o.value(QStringLiteral("approved_by")).toString();
		row.changes     = o.value(QStringLiteral("changes")).toString();
		rows.append(row);
	}
	return rows;
}

QString PdmReleaseHistory::lastReleaseDate(const QDomDocument &doc)
{
	// 取最大日期(ISO 字串可直接字典序比大小);容忍非單調的手動編修。
	QString latest;
	const QList<Row> rows = readReleases(doc);
	for (const Row &r : rows) {
		if (r.date > latest) latest = r.date;
	}
	return latest;
}

bool PdmReleaseHistory::appendRelease(QDomDocument &doc, const Row &row)
{
	QList<Row> rows = readReleases(doc);
	rows.append(row);

	QJsonArray arr;
	for (const Row &r : rows) {
		arr.append(QJsonObject{
			{QStringLiteral("version"),     r.version},
			{QStringLiteral("date"),        r.date},
			{QStringLiteral("approved_by"), r.approved_by},
			{QStringLiteral("changes"),     r.changes}});
	}
	const QString json = QString::fromUtf8(
		QJsonDocument(arr).toJson(QJsonDocument::Compact));
	return PdmVersion::setProjectProperty(doc,
		QLatin1String(HISTORY_PROP), json);
}
