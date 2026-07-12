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
#ifndef QETLIBRARYREQUIREMENT_H
#define QETLIBRARYREQUIREMENT_H

#include <QString>
#include <QStringList>

/**
	@brief 檢查本機元件庫(公司圖框 titleblocks-company、專案範本
	Project-Example)版本是否落後於「此軟體版本所綁定的最低版本」。

	綁定版本存於編進資源的 :/library-requirements.json,由 OTA 發行流程
	於每次發行時更新。啟動時比對本機 dataDir 內元件庫的檔名版本 token;
	若過舊則回傳待更新項,呼叫端提醒使用者更新元件庫。
	見 [[qet-macos-macdeployqt-crash]] 相關 OTA 流程。
*/
namespace QetLibraryRequirement
{
	/// 綁定的最低版本(讀自 :/library-requirements.json)
	struct Requirement {
		QString project_template;   ///< 如 "v0.6";空 = 不檢查此項
		QString titleblock;         ///< 如 "v1.2";空 = 不檢查此項
	};

	/// 一項落後的元件庫
	struct Outdated {
		QString display;    ///< 顯示名(如「專案範本」)
		QString required;   ///< 需求版本
		QString local;      ///< 本機版本(空=未安裝)
	};

	/// 讀取綁定需求(資源缺失/解析失敗回空需求 = 不提醒)
	Requirement required();

	/**
		比對本機版本與 @a req,回傳所有落後(本機 < 需求)的項目。
		空清單 = 全部符合或無需檢查。
	*/
	QList<Outdated> outdated(const Requirement &req);

	/// 便捷:直接讀需求並回傳落後項
	QList<Outdated> outdated();
}

#endif // QETLIBRARYREQUIREMENT_H
