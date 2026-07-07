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
#ifndef PDMSERVICE_H
#define PDMSERVICE_H

#include <QJsonDocument>
#include <QObject>

#include <functional>

class QNetworkAccessManager;

/**
	@brief Gitea REST API v1 客戶端(PDM 用)。
	以 access token 認證;所有呼叫皆非同步,回呼在主執行緒執行。
	流程狀態(鎖定/PR/發行)一律即時從 Gitea 推導,本類別不快取。
*/
class PdmService : public QObject
{
	Q_OBJECT

	public:
		struct Reply {
			bool ok = false;          ///< 傳輸成功且 http 2xx
			int http_status = 0;
			QJsonDocument json;
			QString error;            ///< 給使用者看的錯誤描述
		};
		using Callback = std::function<void (const Reply &)>;

		explicit PdmService(QObject *parent = nullptr);

		/// GET /api/v1/user — 驗證伺服器與 token,成功時回傳登入帳號
		void verifyConnection(const std::function<void (bool ok,
			const QString &login_or_error)> &done);

		/// GET /api/v1/repos/search — 列出 token 可存取的 repo
		void listRepositories(Callback done);

		void get(const QString &api_path, Callback done);

	private:
		QNetworkAccessManager *m_network = nullptr;
};

#endif // PDMSERVICE_H
