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
#include <QJsonObject>
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

		/**
			POST /api/v1/users/{user}/tokens (Basic Auth)。
			用帳號密碼替使用者自動產生一組 access token,回傳 token 字串;
			使用者不必自行到 Gitea 網頁產生。密碼僅用於本次呼叫,不儲存。
		*/
		void createTokenWithPassword(const QString &username,
			const QString &password,
			const std::function<void (bool ok,
				const QString &token_or_error)> &done);

		/// GET /api/v1/repos/search — 列出 token 可存取的 repo
		void listRepositories(Callback done);

		/// GET /repos/{repo}/pulls?state=open
		void listOpenPullRequests(const QString &repo_full_name,
					  Callback done);

		/// POST /repos/{repo}/pulls — 送審(head → base 開 PR)
		void createPullRequest(const QString &repo_full_name,
				       const QString &head_branch,
				       const QString &base_branch,
				       const QString &title,
				       const QString &body,
				       Callback done);

		/// POST /repos/{repo}/pulls/{index}/reviews —
		/// event = "APPROVED" 或 "REQUEST_CHANGES"
		void submitReview(const QString &repo_full_name, int pr_index,
				  const QString &event, const QString &body,
				  Callback done);

		/**
			POST /repos/{repo}/pulls/{index}/merge。
			核准後 Gitea 的可合併狀態有極短暫延遲(§4 驗證:瞬態
			405),故失敗時每秒重試,最多 max_retries 次。
		*/
		void mergePullRequest(const QString &repo_full_name, int pr_index,
				      Callback done, int max_retries = 3);

		/// POST /repos/{repo}/releases
		void createRelease(const QString &repo_full_name,
				   const QString &tag_name,
				   const QString &target_commitish,
				   const QString &title, const QString &body,
				   Callback done);

		/// POST /repos/{repo}/releases/{id}/assets — 上傳發行附件
		void uploadReleaseAsset(const QString &repo_full_name,
					qint64 release_id,
					const QString &file_path,
					Callback done);

		void get(const QString &api_path, Callback done);
		void post(const QString &api_path, const QJsonObject &body,
			  Callback done);

	private:
		void request(const QByteArray &verb, const QString &api_path,
			     const QByteArray &payload,
			     const QByteArray &content_type, Callback done);

		QNetworkAccessManager *m_network = nullptr;
};

#endif // PDMSERVICE_H
