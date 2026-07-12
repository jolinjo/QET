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
#include "pdmrevision.h"

#include <QDomDocument>
#include <QJsonArray>
#include <QObject>
#include <QStringList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>

namespace
{
	const int ROW_COUNT = 6;
	// 與 FolioRevisionsDialog 的 revKey 一致:rev{1-6}-{idx,date,zone,desc,by,appd}
	QString revKey(int row, const char *field)
	{
		return QStringLiteral("rev%1-%2").arg(row + 1)
			.arg(QLatin1String(field));
	}

	// pdm-meta 機器區塊哨兵
	const QString META_BEGIN = QStringLiteral("<!-- pdm-meta:v1");
	const QString META_END   = QStringLiteral("pdm-meta -->");

	// 讀某頁 <diagram> 的 <properties>/<property name> 文字
	QString diagramProperty(const QDomElement &diagram, const QString &name)
	{
		const QDomElement properties = diagram.firstChildElement(
			QStringLiteral("properties"));
		for (QDomElement p = properties.firstChildElement(
			QStringLiteral("property"));
		     !p.isNull();
		     p = p.nextSiblingElement(QStringLiteral("property"))) {
			if (p.attribute(QStringLiteral("name")) == name)
				return p.text();
		}
		return QString();
	}

	// 寫某頁 <diagram> 的 <properties>/<property name>(找不到則建立);
	// 回傳是否有實際變更
	bool setDiagramProperty(QDomElement &diagram, const QString &name,
				const QString &value)
	{
		QDomDocument doc = diagram.ownerDocument();
		QDomElement properties = diagram.firstChildElement(
			QStringLiteral("properties"));
		if (properties.isNull()) {
			properties = doc.createElement(QStringLiteral("properties"));
			diagram.appendChild(properties);
		}
		QDomElement target;
		for (QDomElement p = properties.firstChildElement(
			QStringLiteral("property"));
		     !p.isNull();
		     p = p.nextSiblingElement(QStringLiteral("property"))) {
			if (p.attribute(QStringLiteral("name")) == name) {
				target = p;
				break;
			}
		}
		if (!target.isNull() && target.text() == value) return false;
		if (target.isNull()) {
			target = doc.createElement(QStringLiteral("property"));
			target.setAttribute(QStringLiteral("name"), name);
			target.setAttribute(QStringLiteral("show"), QStringLiteral("1"));
			properties.appendChild(target);
		}
		while (target.hasChildNodes())
			target.removeChild(target.firstChild());
		target.appendChild(doc.createTextNode(value));
		return true;
	}
}

QDate PdmRevision::parseRevDate(const QString &s)
{
	const QString t = s.trimmed();
	if (t.isEmpty()) return QDate();
	static const char *formats[] = {
		"yyyy/M/d", "yyyy/MM/dd", "yyyy-MM-dd", "yyyy-M-d", "yyyyMMdd" };
	for (const char *fmt : formats) {
		const QDate d = QDate::fromString(t, QLatin1String(fmt));
		if (d.isValid()) return d;
	}
	return QDate();
}

QList<PdmRevision::Entry> PdmRevision::collectChanges(const QDomDocument &doc)
{
	QList<Entry> entries;
	int position = 0;
	for (QDomElement diagram = doc.documentElement()
		.firstChildElement(QStringLiteral("diagram"));
	     !diagram.isNull();
	     diagram = diagram.nextSiblingElement(QStringLiteral("diagram"))) {
		const int index = position;   // 0-based 位置(回填核准者用)
		++position;
		// 頁碼:優先 order 屬性,否則用位置+1
		bool order_ok = false;
		const int order = diagram.attribute(QStringLiteral("order"))
			.toInt(&order_ok);
		const int folio = order_ok && order > 0 ? order : position;
		const QString title = diagram.attribute(QStringLiteral("title"))
			.trimmed();

		for (int row = 0; row < ROW_COUNT; ++row) {
			const QString desc = diagramProperty(diagram,
				revKey(row, "desc")).trimmed();
			if (desc.isEmpty()) continue;
			// 核准者(appd)已填 = 已發行過,不再收;空 = 待發行。
			const QString appd = diagramProperty(diagram,
				revKey(row, "appd")).trimmed();
			if (!appd.isEmpty()) continue;

			const QString date_str = diagramProperty(diagram,
				revKey(row, "date")).trimmed();
			Entry e;
			e.folio = folio;
			e.diagram_index = index;
			e.row = row;
			e.title = title;
			e.date = date_str;
			e.zone = diagramProperty(diagram, revKey(row, "zone")).trimmed();
			e.desc = desc;
			e.by = diagramProperty(diagram, revKey(row, "by")).trimmed();
			e.parsed_date = parseRevDate(date_str);
			entries.append(e);
		}
	}

	std::stable_sort(entries.begin(), entries.end(),
		[](const Entry &a, const Entry &b) {
			if (a.folio != b.folio) return a.folio < b.folio;
			return a.parsed_date < b.parsed_date;
		});
	return entries;
}

bool PdmRevision::fillApprover(QDomDocument &doc, const QList<Entry> &entries,
			      const QString &approver)
{
	if (approver.isEmpty() || entries.isEmpty()) return false;

	// 依 diagram_index 分組要回填的 (row) 列
	QMap<int, QList<int>> rows_by_diagram;
	for (const Entry &e : entries)
		if (e.diagram_index >= 0 && e.row >= 0)
			rows_by_diagram[e.diagram_index].append(e.row);

	bool changed = false;
	int position = 0;
	for (QDomElement diagram = doc.documentElement()
		.firstChildElement(QStringLiteral("diagram"));
	     !diagram.isNull();
	     diagram = diagram.nextSiblingElement(QStringLiteral("diagram")),
	     ++position) {
		if (!rows_by_diagram.contains(position)) continue;
		for (int row : rows_by_diagram.value(position)) {
			if (setDiagramProperty(diagram, revKey(row, "appd"),
					       approver))
				changed = true;
		}
	}
	return changed;
}

QString PdmRevision::formatChanges(const QList<Entry> &entries)
{
	// 格式:P{頁碼}.座標-修改內容/座標-修改內容 …,同頁多筆以 / 分隔,
	// 跨頁以空白分隔。座標為空則只列修改內容(避免出現前導「-」)。
	QMap<int, QStringList> by_folio;   // QMap 依 key 排序 = 依頁碼
	for (const Entry &e : entries) {
		const QString zone = e.zone.trimmed();
		const QString item = zone.isEmpty()
			? e.desc
			: QStringLiteral("%1-%2").arg(zone, e.desc);
		by_folio[e.folio].append(item);
	}

	QStringList parts;
	for (auto it = by_folio.constBegin(); it != by_folio.constEnd(); ++it) {
		parts.append(QStringLiteral("P%1.%2")
			.arg(it.key()).arg(it.value().join(QLatin1Char('/'))));
	}
	return parts.join(QLatin1Char(' '));
}

QStringList PdmRevision::incompleteRows(const QDomDocument &doc)
{
	QStringList problems;
	int position = 0;
	for (QDomElement diagram = doc.documentElement()
		.firstChildElement(QStringLiteral("diagram"));
	     !diagram.isNull();
	     diagram = diagram.nextSiblingElement(QStringLiteral("diagram"))) {
		++position;
		bool order_ok = false;
		const int order = diagram.attribute(QStringLiteral("order"))
			.toInt(&order_ok);
		const int folio = order_ok && order > 0 ? order : position;
		const QString title = diagram.attribute(QStringLiteral("title"))
			.trimmed();
		const QString folio_label = title.isEmpty()
			? QObject::tr("第 %1 頁").arg(folio)
			: QObject::tr("第 %1 頁(%2)").arg(folio).arg(title);

		for (int row = 0; row < ROW_COUNT; ++row) {
			const QString date = diagramProperty(diagram,
				revKey(row, "date")).trimmed();
			const QString zone = diagramProperty(diagram,
				revKey(row, "zone")).trimmed();
			const QString desc = diagramProperty(diagram,
				revKey(row, "desc")).trimmed();
			// 「有動過」= 日期、座標或修改內容任一非空;版次/修改者為系統
			// 自動帶入,不作為判斷依據。
			const bool touched = !date.isEmpty() || !zone.isEmpty()
					     || !desc.isEmpty();
			if (!touched) continue;
			if (desc.isEmpty()) {
				problems << QObject::tr(
					"%1 第 %2 列:有資料但缺「修改內容」")
					.arg(folio_label).arg(row + 1);
			} else if (date.isEmpty()) {
				problems << QObject::tr(
					"%1 第 %2 列:有修改內容但缺「日期」")
					.arg(folio_label).arg(row + 1);
			}
		}
	}
	return problems;
}

QString PdmRevision::encodeCommit(const QString &human_summary,
				  const CommitMeta &meta)
{
	QJsonArray changes;
	for (const Entry &e : meta.changes) {
		changes.append(QJsonObject{
			{QStringLiteral("folio"), e.folio},
			{QStringLiteral("date"),  e.date},
			{QStringLiteral("zone"),  e.zone},
			{QStringLiteral("desc"),  e.desc}});
	}
	const QJsonObject root{
		{QStringLiteral("work_version"),      meta.work_version},
		{QStringLiteral("release_base_date"), meta.release_base_date},
		{QStringLiteral("changes"),           changes},
		{QStringLiteral("comment"),           meta.comment}};
	const QString json = QString::fromUtf8(
		QJsonDocument(root).toJson(QJsonDocument::Indented));

	return human_summary.trimmed() + QStringLiteral("\n\n")
		+ META_BEGIN + QStringLiteral("\n") + json + META_END
		+ QStringLiteral("\n");
}

bool PdmRevision::decodeCommit(const QString &commit_message, CommitMeta *out)
{
	if (!out) return false;
	const int begin = commit_message.indexOf(META_BEGIN);
	if (begin < 0) return false;
	const int json_start = begin + META_BEGIN.length();
	const int end = commit_message.indexOf(META_END, json_start);
	if (end < 0) return false;

	const QString json = commit_message.mid(json_start, end - json_start);
	QJsonParseError err;
	const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
	if (err.error != QJsonParseError::NoError || !doc.isObject())
		return false;

	const QJsonObject root = doc.object();
	out->work_version = root.value(QStringLiteral("work_version")).toString();
	out->release_base_date =
		root.value(QStringLiteral("release_base_date")).toString();
	out->comment = root.value(QStringLiteral("comment")).toString();
	out->changes.clear();
	const QJsonArray changes = root.value(QStringLiteral("changes")).toArray();
	for (const QJsonValue &v : changes) {
		const QJsonObject o = v.toObject();
		Entry e;
		e.folio = o.value(QStringLiteral("folio")).toInt();
		e.date  = o.value(QStringLiteral("date")).toString();
		e.zone  = o.value(QStringLiteral("zone")).toString();
		e.desc  = o.value(QStringLiteral("desc")).toString();
		e.parsed_date = parseRevDate(e.date);
		out->changes.append(e);
	}
	return true;
}
