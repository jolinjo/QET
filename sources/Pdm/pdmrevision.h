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
#ifndef PDMREVISION_H
#define PDMREVISION_H

#include <QDate>
#include <QList>
#include <QString>

class QDomDocument;

/**
	@brief PDM 修訂項的收集與結構化 commit 編解碼。
	見 doc/pdm-revision-design.md §3(比對)、§4(commit 格式)。
	比對純用日期過濾(不做圖差);頁身分用當下頁碼(order 屬性/位置),
	因為 .qet 不持久化 diagram uuid,而日期過濾不需跨 commit 對應身分。
*/
namespace PdmRevision
{
	/// 一筆修訂項(讀自某頁圖框的 rev1..rev6 之一)
	struct Entry {
		int folio = 0;          ///< 頁碼(1-based;order 屬性或位置)
		QString date;           ///< 原始日期字串(顯示用,如 2026/5/31)
		QString desc;           ///< 修訂說明
		QDate parsed_date;      ///< 解析後日期(比對用)

		bool operator==(const Entry &o) const {
			return folio == o.folio && date == o.date && desc == o.desc;
		}
	};

	/// 寬鬆解析修訂日期:接受 yyyy/M/d、yyyy-MM-dd、yyyyMMdd
	QDate parseRevDate(const QString &s);

	/**
		收集 .qet 各頁修訂欄中「日期 >= since」且說明非空的修訂項。
		@param since 無效 QDate 代表全收(不過濾)。回傳依頁碼、日期排序。
	*/
	QList<Entry> collectChanges(const QDomDocument &doc, const QDate &since);

	/// 把一組修訂項組成單一 changes 字串:「P3.說明 P7.說明」,同頁以 ; 併
	QString formatChanges(const QList<Entry> &entries);

	/// 結構化 commit 內容(§4)
	struct CommitMeta {
		QString work_version;
		QString release_base_date;   ///< 比對基準日(ISO),稽核用
		QList<Entry> changes;        ///< 本次納入的修訂項
		QString comment;             ///< 使用者出入庫意見(不進發行史)
	};

	/// 編碼:人類摘要 + <!-- pdm-meta:v1 {json} pdm-meta --> 機器區塊
	QString encodeCommit(const QString &human_summary, const CommitMeta &meta);

	/// 解碼機器區塊;找不到/解析失敗回 false(呼叫端降級處理)
	bool decodeCommit(const QString &commit_message, CommitMeta *out);
}

#endif // PDMREVISION_H
