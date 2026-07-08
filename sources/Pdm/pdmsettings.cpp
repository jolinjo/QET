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
#include "pdmsettings.h"

#include <QDir>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

namespace
{
	const char *KEY_SERVER_URL = "pdm/server-url";
	const char *KEY_USERNAME   = "pdm/username";
	const char *KEY_TOKEN      = "pdm/token";
	const char *KEY_WORK_ROOT  = "pdm/work-root";
	const char *KEY_REPO       = "pdm/repo";

	// 內網 Gitea 預設主機:沿用 OTA 更新器既有的伺服器(ota/repo-url)。
	const char *DEFAULT_SERVER = "http://hc-server:3000";
	// 公司圖庫:圖檔管理固定連此 repo,不提供選擇。
	const char *DEFAULT_REPO   = "HC-Git/HC_Electrical-Schematics";

#ifdef Q_OS_MACOS
	const char *KEYCHAIN_SERVICE = "qelectrotech-pdm-gitea";

	// macOS 用系統鑰匙圈存 token,不落任何設定檔。
	QString keychainRead(const QString &account)
	{
		QProcess p;
		p.start("security", {"find-generic-password", "-s", KEYCHAIN_SERVICE,
			"-a", account, "-w"});
		if (!p.waitForFinished(5000) || p.exitCode() != 0) return QString();
		return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
	}

	void keychainWrite(const QString &account, const QString &token)
	{
		QProcess p;
		if (token.isEmpty()) {
			p.start("security", {"delete-generic-password", "-s",
				KEYCHAIN_SERVICE, "-a", account});
		} else {
			// -U:已存在則更新
			p.start("security", {"add-generic-password", "-U", "-s",
				KEYCHAIN_SERVICE, "-a", account, "-w", token});
		}
		p.waitForFinished(5000);
	}
#endif
}

QString PdmSettings::serverUrl()
{
	QSettings settings;
	QString url = settings.value(KEY_SERVER_URL, DEFAULT_SERVER).toString().trimmed();
	while (url.endsWith('/')) url.chop(1);
	return url;
}

void PdmSettings::setServerUrl(const QString &url)
{
	QSettings().setValue(KEY_SERVER_URL, url.trimmed());
}

QString PdmSettings::username()
{
	return QSettings().value(KEY_USERNAME).toString();
}

void PdmSettings::setUsername(const QString &name)
{
	QSettings().setValue(KEY_USERNAME, name);
}

QString PdmSettings::token()
{
#ifdef Q_OS_MACOS
	// 固定帳號鍵:token 不隨 username 變動(登入填 email、連線後改成 login,
	// 若以 username 當鍵會在中途讀不到已存的 token)。
	return keychainRead(QStringLiteral("token"));
#else
	// TODO(Windows):改用 DPAPI(CryptProtectData)。base64 只是避免
	// 設定檔被肉眼直讀,不是加密;內網環境暫時接受,正式部署前要換掉。
	const QByteArray stored = QSettings().value(KEY_TOKEN).toByteArray();
	return QString::fromUtf8(QByteArray::fromBase64(stored));
#endif
}

void PdmSettings::setToken(const QString &token)
{
#ifdef Q_OS_MACOS
	keychainWrite(QStringLiteral("token"), token);
#else
	if (token.isEmpty()) {
		QSettings().remove(KEY_TOKEN);
	} else {
		QSettings().setValue(KEY_TOKEN, token.toUtf8().toBase64());
	}
#endif
}

QString PdmSettings::workRoot()
{
	QSettings settings;
	QString dir = settings.value(KEY_WORK_ROOT).toString();
	if (dir.isEmpty()) {
		dir = QStandardPaths::writableLocation(
			QStandardPaths::DocumentsLocation) + "/QET-PDM";
	}
	return QDir::cleanPath(dir);
}

void PdmSettings::setWorkRoot(const QString &dir)
{
	QSettings().setValue(KEY_WORK_ROOT, QDir::cleanPath(dir));
}

QString PdmSettings::repo()
{
	QSettings settings;
	const QString value = settings.value(KEY_REPO).toString().trimmed();
	return value.isEmpty() ? QString::fromUtf8(DEFAULT_REPO) : value;
}

void PdmSettings::setRepo(const QString &full_name)
{
	QSettings().setValue(KEY_REPO, full_name.trimmed());
}
