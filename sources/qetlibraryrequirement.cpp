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
#include "qetlibraryrequirement.h"

#include "qetapp.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QRegularExpression>

namespace
{
	// 從字串取出版本 token「vX[.Y[.Z]]」(與元件庫更新面板一致)
	QString versionToken(const QString &s)
	{
		static const QRegularExpression re(
			QStringLiteral("v\\d+(?:\\.\\d+)*"),
			QRegularExpression::CaseInsensitiveOption);
		const QRegularExpressionMatch m = re.match(s);
		return m.hasMatch() ? m.captured(0) : QString();
	}

	// 版本比較:去掉前導 v、逐段數字比。a<b 回負、相等 0、a>b 正。
	// 逐段數字比(非字典序),避免 v0.10 < v0.6 的錯誤。
	int compareVersion(const QString &a, const QString &b)
	{
		auto parts = [](const QString &v) {
			QString t = v;
			if (t.startsWith(QLatin1Char('v')) || t.startsWith(QLatin1Char('V')))
				t = t.mid(1);
			QList<int> out;
			const QStringList segs = t.split(QLatin1Char('.'),
				Qt::SkipEmptyParts);
			for (const QString &s : segs) out << s.toInt();
			return out;
		};
		const QList<int> pa = parts(a), pb = parts(b);
		const int n = qMax(pa.size(), pb.size());
		for (int i = 0; i < n; ++i) {
			const int x = i < pa.size() ? pa.at(i) : 0;
			const int y = i < pb.size() ? pb.at(i) : 0;
			if (x != y) return x < y ? -1 : 1;
		}
		return 0;
	}

	// dataDir/subdir 內符合 pattern 的檔名中,最大的版本 token(空=沒有/未安裝)
	QString localVersion(const QString &subdir, const QString &pattern)
	{
		const QString dir_path = QETApp::dataDir() % QChar('/') % subdir;
		QDir dir(dir_path);
		if (!dir.exists()) return QString();
		QString best;
		const QFileInfoList list = dir.entryInfoList(
			QStringList{pattern}, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
		for (const QFileInfo &fi : list) {
			const QString v = versionToken(fi.fileName());
			if (!v.isEmpty() && (best.isEmpty() || compareVersion(v, best) > 0))
				best = v;
		}
		return best;
	}
}

QetLibraryRequirement::Requirement QetLibraryRequirement::required()
{
	Requirement req;
	QFile f(QStringLiteral(":/library-requirements.json"));
	if (!f.open(QIODevice::ReadOnly)) return req;
	const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
	f.close();
	if (!doc.isObject()) return req;
	const QJsonObject o = doc.object();
	req.project_template =
		o.value(QStringLiteral("project_template")).toString().trimmed();
	req.titleblock = o.value(QStringLiteral("titleblock")).toString().trimmed();
	return req;
}

QList<QetLibraryRequirement::Outdated> QetLibraryRequirement::outdated(
	const Requirement &req)
{
	QList<Outdated> result;

	// 只在「本機已安裝且版本較舊」時提醒(使用者要求:版本比較舊才提醒)。
	// 本機版本為空 = 未在 dataDir 安裝(可能用自訂路徑/尚未安裝),不誤報。

	// 專案範本:Project-Example 內 *.qet 檔名版本
	if (!req.project_template.isEmpty()) {
		const QString local = localVersion(
			QStringLiteral("Project-Example"), QStringLiteral("*.qet"));
		if (!local.isEmpty()
		    && compareVersion(local, req.project_template) < 0) {
			result << Outdated{
				QObject::tr("專案範本"), req.project_template, local};
		}
	}

	// 公司圖框:titleblocks-company 內檔名版本
	if (!req.titleblock.isEmpty()) {
		const QString local = localVersion(
			QStringLiteral("titleblocks-company"),
			QStringLiteral("*.titleblock"));
		if (!local.isEmpty() && compareVersion(local, req.titleblock) < 0) {
			result << Outdated{
				QObject::tr("公司圖框"), req.titleblock, local};
		}
	}

	return result;
}

QList<QetLibraryRequirement::Outdated> QetLibraryRequirement::outdated()
{
	return outdated(required());
}
