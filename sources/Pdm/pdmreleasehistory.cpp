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

// 兼作辨識標記:此字串會出現在 HTML 表格的標題列,存檔 toHtml round-trip
// 後仍保留(是文件內容,非屬性),故可用「text 含此字串」認出既有的發行史表。
const char *const PdmReleaseHistory::HISTORY_HEADER =
	"文件修訂記錄 Revision History";

const double PdmReleaseHistory::HISTORY_TABLE_MARGIN = 40.0;

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
		row.modified_by = o.value(QStringLiteral("modified_by")).toString();
		row.approved_by = o.value(QStringLiteral("approved_by")).toString();
		row.status      = o.value(QStringLiteral("status")).toString();
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

namespace
{
	bool writeReleases(QDomDocument &doc, const QList<PdmReleaseHistory::Row> &rows)
	{
		QJsonArray arr;
		for (const PdmReleaseHistory::Row &r : rows) {
			arr.append(QJsonObject{
				{QStringLiteral("version"),     r.version},
				{QStringLiteral("date"),        r.date},
				{QStringLiteral("modified_by"), r.modified_by},
				{QStringLiteral("approved_by"), r.approved_by},
				{QStringLiteral("status"),      r.status},
				{QStringLiteral("changes"),     r.changes}});
		}
		const QString json = QString::fromUtf8(
			QJsonDocument(arr).toJson(QJsonDocument::Compact));
		return PdmVersion::setProjectProperty(doc,
			QLatin1String(HISTORY_PROP), json);
	}
}

bool PdmReleaseHistory::appendRelease(QDomDocument &doc, const Row &row)
{
	QList<Row> rows = readReleases(doc);
	rows.append(row);
	return writeReleases(doc, rows);
}

bool PdmReleaseHistory::upsertPendingRow(QDomDocument &doc,
					 const QString &changes,
					 const QString &modified_by,
					 const QString &status)
{
	QList<Row> rows = readReleases(doc);
	for (Row &r : rows) {
		if (r.version.isEmpty()) {   // 已有待發行列 → 更新異動/修改者/狀態
			r.changes = changes;
			r.modified_by = modified_by;
			r.status = status;
			return writeReleases(doc, rows);
		}
	}
	// 新增待發行列(版本/日期/核准者留空)
	rows.append({QString(), QString(), modified_by, QString(), status, changes});
	return writeReleases(doc, rows);
}

bool PdmReleaseHistory::finalizePending(QDomDocument &doc, const Row &row)
{
	QList<Row> rows = readReleases(doc);
	for (Row &r : rows) {
		if (r.version.isEmpty()) {   // 待發行列 → 填上版本/日期/核准者/異動
			r = row;
			return writeReleases(doc, rows);
		}
	}
	rows.append(row);   // 無待發行列(例如舊檔)→ 直接 append
	return writeReleases(doc, rows);
}

namespace
{
	// 最小 HTML escape(避免異動摘要裡的 < > & 破壞表格)
	QString esc(const QString &s)
	{
		QString o = s;
		o.replace(QLatin1Char('&'), QLatin1String("&amp;"));
		o.replace(QLatin1Char('<'), QLatin1String("&lt;"));
		o.replace(QLatin1Char('>'), QLatin1String("&gt;"));
		return o;
	}
}

QString PdmReleaseHistory::formatHistoryText(const QList<Row> &rows,
					    double table_width)
{
	// 產生真正的 HTML 表格(有框線),IndependentTextItem 會以 rich text
	// 呈現。不指定 Menlo 之類無 CJK 字符的字型,交給圖元預設字型(公司版
	// 已設為含中文的字型),避免中文變亂碼。標題列含 HISTORY_HEADER 供辨識。
	// 表寬由呼叫端傳入(≈頁寬-2*邊距),各欄寬加總=table_width
	// 明確欄寬(加總=table_width)強制表格達到指定總寬——QTextDocument 對
	// <table width> 只當建議值、會縮成內容寬,故改為固定每欄寬度撐滿。
	// 版本/日期/核准固定,異動(修改內容)吃剩餘寬度 → 表寬≈頁寬時異動欄很寬。
	const int total = qMax(300, int(table_width));
	// 固定欄:版本/日期/修改者/核准/文件狀態;異動吃剩餘寬度。
	const int w_ver = 70, w_date = 110, w_by = 90, w_appr = 90, w_status = 110,
		  w_chg = qMax(100, total - w_ver - w_date - w_by - w_appr - w_status);
	QString html =
		QStringLiteral("<table border=\"1\" cellspacing=\"0\" "
			"cellpadding=\"4\" align=\"center\" width=\"%1\">")
			.arg(total);
	// HISTORY_HEADER 是 UTF-8 char*;必須用 fromUtf8 解碼,不能用
	// QLatin1String(會把 UTF-8 位元組當 Latin-1 → 中文變亂碼)。
	html += QStringLiteral(
		"<tr><td colspan=\"6\" align=\"center\"><b>%1</b></td></tr>")
		.arg(QString::fromUtf8(HISTORY_HEADER));
	html += QStringLiteral(
		"<tr><td width=\"%1\" align=\"center\"><b>版本</b></td>"
		"<td width=\"%2\" align=\"center\"><b>日期</b></td>"
		"<td width=\"%3\" align=\"center\"><b>修改者</b></td>"
		"<td width=\"%4\" align=\"center\"><b>核准</b></td>"
		"<td width=\"%5\" align=\"center\"><b>文件狀態</b></td>"
		"<td width=\"%6\" align=\"center\"><b>異動</b></td></tr>")
		.arg(w_ver).arg(w_date).arg(w_by).arg(w_appr).arg(w_status).arg(w_chg);
	for (const Row &r : rows) {
		html += QStringLiteral(
			"<tr><td width=\"%1\" align=\"center\">%7</td>"
			"<td width=\"%2\" align=\"center\">%8</td>"
			"<td width=\"%3\" align=\"center\">%9</td>"
			"<td width=\"%4\" align=\"center\">%10</td>"
			"<td width=\"%5\" align=\"center\">%11</td>"
			"<td width=\"%6\">%12</td></tr>")
			.arg(w_ver).arg(w_date).arg(w_by).arg(w_appr)
			.arg(w_status).arg(w_chg)
			.arg(esc(r.version), esc(r.date), esc(r.modified_by),
			     esc(r.approved_by), esc(r.status), esc(r.changes));
	}
	html += QStringLiteral("</table>");
	return html;
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

	// 位置(default_x/default_y)由呼叫端以實際量測的表格寬度算好置中值傳入。
	// 既有項也重新套用位置 → 每次發行都重新置中(表格變寬時仍居中)。
	const QString header = QString::fromUtf8(HISTORY_HEADER);
	for (QDomElement in = inputs.firstChildElement(QStringLiteral("input"));
	     !in.isNull(); in = in.nextSiblingElement(QStringLiteral("input"))) {
		if (in.attribute(QStringLiteral("text")).contains(header)) {
			in.setAttribute(QStringLiteral("text"), history_text);
			in.setAttribute(QStringLiteral("x"), QString::number(default_x));
			in.setAttribute(QStringLiteral("y"), QString::number(default_y));
			if (!font_string.isEmpty())
				in.setAttribute(QStringLiteral("font"), font_string);
			return true;
		}
	}

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
