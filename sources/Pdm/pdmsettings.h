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
#ifndef PDMSETTINGS_H
#define PDMSETTINGS_H

#include <QString>

/**
	@brief PDM(圖檔管理)設定的集中存取。
	所有鍵都放在 QSettings 的 "pdm/" 群組下;token 依平台以較安全的方式
	儲存(macOS: Keychain;其他平台暫以混淆後存 QSettings,見 token())。
*/
namespace PdmSettings
{
	QString serverUrl();
	void setServerUrl(const QString &url);

	QString username();
	void setUsername(const QString &name);

	QString token();
	void setToken(const QString &token);

	QString workRoot();
	void setWorkRoot(const QString &dir);

	/// 固定圖庫(owner/repo);非空時圖檔管理只連此 repo,不列出其他。
	QString repo();
	void setRepo(const QString &full_name);
}

#endif // PDMSETTINGS_H
