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

const char *const PdmReleaseHistory::HISTORY_HEADER =
	"【文件修訂記錄 Revision History】";

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

QString PdmReleaseHistory::formatHistoryText(const QList<Row> &rows)
{
	// 等寬字型下用空白對齊成類表格。版本/日期欄定寬,異動欄不截斷。
	QString text = QLatin1String(HISTORY_HEADER);
	for (const Row &r : rows) {
		text += QStringLiteral("\n%1  %2  %3  %4")
			.arg(r.version, -6)      // 版本靠左補到 6 寬
			.arg(r.date, -10)        // ISO 日期固定 10 寬
			.arg(r.approved_by, -6)  // 核准者靠左補到 6 寬
			.arg(r.changes);
	}
	return text;
}

bool PdmReleaseHistory::upsertHistoryTextItem(QDomDocument &doc,
	const QString &history_text, const QString &font_string,
	double default_x, double default_y)
{
	QDomElement project = doc.documentElement();
	if (project.isNull()) return false;
	// 文件管制頁:目前取第一個 <diagram>(設計 §5.2;日後可改 UUID 釘頁)
	QDomElement diagram = project.firstChildElement(QStringLiteral("diagram"));
	if (diagram.isNull()) return false;

	QDomElement inputs = diagram.firstChildElement(QStringLiteral("inputs"));
	if (inputs.isNull()) {
		inputs = doc.createElement(QStringLiteral("inputs"));
		diagram.appendChild(inputs);
	}

	// 以「text 以 HISTORY_HEADER 起首」辨識既有的發行史文字圖元
	const QString header = QLatin1String(HISTORY_HEADER);
	for (QDomElement in = inputs.firstChildElement(QStringLiteral("input"));
	     !in.isNull(); in = in.nextSiblingElement(QStringLiteral("input"))) {
		if (in.attribute(QStringLiteral("text")).startsWith(header)) {
			// 只更新內容,保留使用者可能調整過的位置/字型
			in.setAttribute(QStringLiteral("text"), history_text);
			return true;
		}
	}

	// 沒有則新建一個,套用預設位置與(等寬)字型
	QDomElement in = doc.createElement(QStringLiteral("input"));
	in.setAttribute(QStringLiteral("x"), QString::number(default_x));
	in.setAttribute(QStringLiteral("y"), QString::number(default_y));
	in.setAttribute(QStringLiteral("text"), history_text);
	in.setAttribute(QStringLiteral("rotation"), QStringLiteral("0"));
	if (!font_string.isEmpty())
		in.setAttribute(QStringLiteral("font"), font_string);
	inputs.appendChild(in);
	return true;
}
