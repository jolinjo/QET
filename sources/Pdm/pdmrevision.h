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
		int folio = 0;          ///< 顯示用頁碼(order 屬性或位置+1)
		int diagram_index = -1; ///< 0-based 頁位置(發行時回填核准者用,穩定)
		int row = -1;           ///< rev 列 index(0..5;回填核准者用)
		QString title;          ///< 子圖名(diagram 的 title 屬性)
		QString date;           ///< 原始日期字串(顯示用,如 2026/5/31)
		QString zone;           ///< 座標(修訂位置)
		QString desc;           ///< 修訂說明
		QString by;             ///< 修改者
		QDate parsed_date;      ///< 解析後日期(排序用)

		bool operator==(const Entry &o) const {
			return folio == o.folio && date == o.date && desc == o.desc;
		}
	};

	/// 寬鬆解析修訂日期:接受 yyyy/M/d、yyyy-MM-dd、yyyyMMdd
	QDate parseRevDate(const QString &s);

	/**
		收集「待發行」修訂項:各頁 rev1..rev6 中「說明非空且核准者(appd)
		為空」者。核准者已填=已發行過,不再收。回傳依頁碼、日期排序。
		見 doc/pdm-revision-design.md(核准者空=待發行狀態機)。
	*/
	QList<Entry> collectChanges(const QDomDocument &doc);

	/**
		發行時把選中修訂項的核准者(appd)欄位填上 @a approver。
		依 Entry 的 diagram_index/row 定位到該頁該列。回傳是否有變更。
	*/
	bool fillApprover(QDomDocument &doc, const QList<Entry> &entries,
			  const QString &approver);

	/**
		核准發行時,對本次有異動的各子頁(@a released 內出現的 diagram_index)
		寫入圖框的「發行日期」(date 屬性,yyyyMMdd)與「修訂索引」(indexrev
		屬性,取該頁修訂列最大版次,如 1、2、3)。回傳是否有變更。
	*/
	bool stampReleaseInfo(QDomDocument &doc, const QList<Entry> &released,
			      const QString &release_date_yyyymmdd);

	/// 把一組修訂項組成單一 changes 字串:「P3.說明 P7.說明」,同頁以 ; 併
	QString formatChanges(const QList<Entry> &entries);

	/**
		檢查各頁修訂列是否有「填一半」的情況:某列已有日期或座標,卻沒有
		修改內容(或反之有內容卻缺日期)。回傳每個問題的人類可讀說明
		(如「第 3 頁(子圖名) 第 3 列:有日期但缺『修改內容』」)。
		空清單 = 全部完整。送審前用來卡控,要求使用者補齊或刪除。
	*/
	QStringList incompleteRows(const QDomDocument &doc);

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
