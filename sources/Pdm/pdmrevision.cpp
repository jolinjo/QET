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

QList<PdmRevision::Entry> PdmRevision::collectChanges(const QDomDocument &doc,
						     const QDate &since)
{
	QList<Entry> entries;
	int position = 0;
	for (QDomElement diagram = doc.documentElement()
		.firstChildElement(QStringLiteral("diagram"));
	     !diagram.isNull();
	     diagram = diagram.nextSiblingElement(QStringLiteral("diagram"))) {
		++position;
		// 頁碼:優先 order 屬性,否則用位置
		bool order_ok = false;
		const int order = diagram.attribute(QStringLiteral("order"))
			.toInt(&order_ok);
		const int folio = order_ok && order > 0 ? order : position;

		for (int row = 0; row < ROW_COUNT; ++row) {
			const QString desc = diagramProperty(diagram,
				revKey(row, "desc")).trimmed();
			if (desc.isEmpty()) continue;
			const QString date_str = diagramProperty(diagram,
				revKey(row, "date")).trimmed();
			const QDate parsed = parseRevDate(date_str);
			// since 有效才過濾;無效=全收。日期無法解析者一律收(保守,
			// 寧可多列讓使用者取捨,也不要漏掉真的異動)。
			if (since.isValid() && parsed.isValid() && parsed < since)
				continue;
			Entry e;
			e.folio = folio;
			e.date = date_str;
			e.desc = desc;
			e.parsed_date = parsed;
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

QString PdmRevision::formatChanges(const QList<Entry> &entries)
{
	// 同頁多筆併進同一段:P3.a;b;c   跨頁以空白分隔
	QMap<int, QStringList> by_folio;   // QMap 依 key 排序 = 依頁碼
	for (const Entry &e : entries)
		by_folio[e.folio].append(e.desc);

	QStringList parts;
	for (auto it = by_folio.constBegin(); it != by_folio.constEnd(); ++it) {
		parts.append(QStringLiteral("P%1.%2")
			.arg(it.key()).arg(it.value().join(QLatin1Char(';'))));
	}
	return parts.join(QLatin1Char(' '));
}

QString PdmRevision::encodeCommit(const QString &human_summary,
				  const CommitMeta &meta)
{
	QJsonArray changes;
	for (const Entry &e : meta.changes) {
		changes.append(QJsonObject{
			{QStringLiteral("folio"), e.folio},
			{QStringLiteral("date"),  e.date},
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
		e.desc  = o.value(QStringLiteral("desc")).toString();
		e.parsed_date = parseRevDate(e.date);
		out->changes.append(e);
	}
	return true;
}
