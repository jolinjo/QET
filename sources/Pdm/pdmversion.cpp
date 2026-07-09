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
#include "pdmversion.h"

#include <QDomDocument>
#include <QStringList>

namespace PdmVersion
{
	const char *const WORK_VERSION    = "pdm_work_version";
	const char *const RELEASE_VERSION = "pdm_release_version";
}

namespace
{
	// <project> 的直接子 <properties>(專案級,非各 <diagram> 內的那個)
	QDomElement projectPropertiesElement(const QDomElement &project)
	{
		return project.firstChildElement(QStringLiteral("properties"));
	}
}

QString PdmVersion::projectProperty(const QDomDocument &doc,
				    const QString &name)
{
	const QDomElement project = doc.documentElement();
	if (project.isNull()) return QString();
	const QDomElement properties = projectPropertiesElement(project);
	if (properties.isNull()) return QString();
	for (QDomElement p = properties.firstChildElement(
		QStringLiteral("property"));
	     !p.isNull();
	     p = p.nextSiblingElement(QStringLiteral("property"))) {
		if (p.attribute(QStringLiteral("name")) == name)
			return p.text();
	}
	return QString();
}

bool PdmVersion::setProjectProperty(QDomDocument &doc, const QString &name,
				    const QString &value)
{
	QDomElement project = doc.documentElement();
	if (project.isNull()) return false;

	QDomElement properties = projectPropertiesElement(project);
	if (properties.isNull()) {
		// QET 存檔一定會建 <project>/<properties>,但新檔或極端情況下
		// 可能還沒有,補建以確保 round-trip。
		properties = doc.createElement(QStringLiteral("properties"));
		project.insertBefore(properties, project.firstChild());
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
	if (!target.isNull() && target.text() == value)
		return false;

	if (target.isNull()) {
		target = doc.createElement(QStringLiteral("property"));
		target.setAttribute(QStringLiteral("name"), name);
		// show="0":PDM 內部變數,不在圖框上呈現
		target.setAttribute(QStringLiteral("show"), QStringLiteral("0"));
		properties.appendChild(target);
	}
	while (target.hasChildNodes())
		target.removeChild(target.firstChild());
	target.appendChild(doc.createTextNode(value));
	return true;
}

QString PdmVersion::legacyFirstDiagramIndexRev(const QDomDocument &doc)
{
	return doc.documentElement()
		.firstChildElement(QStringLiteral("diagram"))
		.attribute(QStringLiteral("indexrev"));
}

QString PdmVersion::workVersion(const QDomDocument &doc)
{
	const QString v = projectProperty(doc, QLatin1String(WORK_VERSION));
	// 舊檔(尚未遷移):退回讀首頁 indexrev,讓版本在遷移前仍正確顯示。
	return v.isEmpty() ? legacyFirstDiagramIndexRev(doc) : v;
}

QString PdmVersion::nextMinor(const QString &current)
{
	// 「主.次」→ 次版 +1;純整數 N(已發行版)→ N.1;空/舊字母 → 0.1
	const QStringList parts = current.trimmed().split(QLatin1Char('.'));
	if (parts.size() >= 2) {
		bool maj_ok = false, min_ok = false;
		const int maj = parts.at(0).toInt(&maj_ok);
		const int min = parts.at(1).toInt(&min_ok);
		if (maj_ok && min_ok)
			return QStringLiteral("%1.%2").arg(maj).arg(min + 1);
	}
	bool int_ok = false;
	const int n = current.trimmed().toInt(&int_ok);
	return int_ok ? QStringLiteral("%1.1").arg(n) : QStringLiteral("0.1");
}

QString PdmVersion::nextMajor(const QString &current)
{
	// 發行進版:主版 +1、次版歸零。0.4→1.0、1.3→2.0;空/舊字母→1.0
	const int dot = current.indexOf(QLatin1Char('.'));
	bool ok = false;
	const int maj = (dot >= 0 ? current.left(dot) : current).trimmed()
		.toInt(&ok);
	return QStringLiteral("%1.0").arg((ok ? maj : 0) + 1);
}
