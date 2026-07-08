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
#include "pdmservice.h"

#include "pdmsettings.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

PdmService::PdmService(QObject *parent) :
	QObject(parent),
	m_network(new QNetworkAccessManager(this))
{
}

void PdmService::request(const QByteArray &verb, const QString &api_path,
			 const QByteArray &payload,
			 const QByteArray &content_type, Callback done)
{
	QNetworkRequest network_request(QUrl(PdmSettings::serverUrl()
					     + QStringLiteral("/api/v1")
					     + api_path));
	// token 只進 Authorization 標頭,絕不進 URL 或任何 log。
	network_request.setRawHeader("Authorization",
				     "token " + PdmSettings::token().toUtf8());
	if (!content_type.isEmpty())
		network_request.setHeader(QNetworkRequest::ContentTypeHeader,
					  content_type);
	network_request.setTransferTimeout(30000);

	QNetworkReply *network_reply = m_network->sendCustomRequest(
		network_request, verb, payload);
	connect(network_reply, &QNetworkReply::finished, this,
		[network_reply, done]() {
			Reply reply;
			reply.http_status = network_reply->attribute(
				QNetworkRequest::HttpStatusCodeAttribute).toInt();
			const QByteArray body = network_reply->readAll();
			reply.json = QJsonDocument::fromJson(body);
			if (network_reply->error() == QNetworkReply::NoError
			    && reply.http_status >= 200 && reply.http_status < 300) {
				reply.ok = true;
			} else if (reply.http_status == 401) {
				reply.error = tr("認證失敗:token 無效或已過期。");
			} else if (network_reply->error() != QNetworkReply::NoError
				   && reply.http_status == 0) {
				reply.error = tr("無法連線伺服器:%1")
					.arg(network_reply->errorString());
			} else {
				// Gitea 錯誤通常帶 message 欄位,一併給使用者
				const QString detail = reply.json.object()
					.value(QStringLiteral("message")).toString();
				reply.error = tr("伺服器回應 http %1%2")
					.arg(reply.http_status)
					.arg(detail.isEmpty() ? QString()
							      : ':' + detail);
			}
			network_reply->deleteLater();
			if (done) done(reply);
		});
}

void PdmService::get(const QString &api_path, Callback done)
{
	request("GET", api_path, {}, {}, std::move(done));
}

void PdmService::post(const QString &api_path, const QJsonObject &body,
		      Callback done)
{
	request("POST", api_path, QJsonDocument(body).toJson(QJsonDocument::Compact),
		"application/json", std::move(done));
}

void PdmService::verifyConnection(
	const std::function<void (bool, const QString &)> &done)
{
	get(QStringLiteral("/user"), [done](const Reply &reply) {
		if (!reply.ok) {
			if (done) done(false, reply.error);
			return;
		}
		const QString login = reply.json.object()
			.value(QStringLiteral("login")).toString();
		if (done) done(!login.isEmpty(), login);
	});
}

void PdmService::createTokenWithPassword(
	const QString &username, const QString &password,
	const std::function<void (bool, const QString &)> &done)
{
	// 帳密只進 Basic 認證標頭,不進 URL 或 log;產出 token 後即棄用密碼。
	const QByteArray auth_header = "Basic "
		+ (username + ':' + password).toUtf8().toBase64();

	// 產 token 的端點吃「帳號名(login)」而非 email,故先用帳密查真正的
	// login,再拿它去建 token;使用者填帳號或 email 皆可。
	QNetworkRequest whoami_req(QUrl(PdmSettings::serverUrl()
					+ QStringLiteral("/api/v1/user")));
	whoami_req.setRawHeader("Authorization", auth_header);
	whoami_req.setTransferTimeout(30000);

	QNetworkReply *whoami = m_network->get(whoami_req);
	connect(whoami, &QNetworkReply::finished, this,
		[this, whoami, auth_header, done]() {
		const int status = whoami->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const QJsonDocument json =
			QJsonDocument::fromJson(whoami->readAll());
		const QNetworkReply::NetworkError net_error = whoami->error();
		const QString net_string = whoami->errorString();
		whoami->deleteLater();

		if (status == 401) {
			if (done) done(false, tr("帳號或密碼錯誤"
				"(若已啟用兩階段驗證,請改用手動貼上 token)。"));
			return;
		}
		if (net_error != QNetworkReply::NoError && status == 0) {
			if (done) done(false,
				tr("無法連線伺服器:%1").arg(net_string));
			return;
		}
		const QString login = json.object()
			.value(QStringLiteral("login")).toString();
		if (status < 200 || status >= 300 || login.isEmpty()) {
			const QString detail = json.object()
				.value(QStringLiteral("message")).toString();
			if (done) done(false, tr("登入失敗:http %1%2")
				.arg(status)
				.arg(detail.isEmpty() ? QString()
						      : ':' + detail));
			return;
		}

		// 第二步:用真正的 login 建立 token
		QNetworkRequest token_req(QUrl(PdmSettings::serverUrl()
			+ QStringLiteral("/api/v1/users/") + login
			+ QStringLiteral("/tokens")));
		token_req.setRawHeader("Authorization", auth_header);
		token_req.setHeader(QNetworkRequest::ContentTypeHeader,
				    "application/json");
		token_req.setTransferTimeout(30000);

		// token 名稱在同帳號下需唯一,附上時戳避免與既有 token 撞名。
		const QString token_name = QStringLiteral("qet-pdm-%1")
			.arg(QDateTime::currentSecsSinceEpoch());
		// read:user:連線驗證 GET /user 需要;write:repository:涵蓋圖檔
		// 內容、PR、Release、LFS 鎖。
		const QJsonObject body{
			{QStringLiteral("name"), token_name},
			{QStringLiteral("scopes"),
			 QJsonArray{QStringLiteral("write:repository"),
				    QStringLiteral("read:user")}}};

		QNetworkReply *reply = m_network->post(token_req,
			QJsonDocument(body).toJson(QJsonDocument::Compact));
		connect(reply, &QNetworkReply::finished, this,
			[reply, done]() {
			const int status = reply->attribute(
				QNetworkRequest::HttpStatusCodeAttribute).toInt();
			const QJsonDocument json =
				QJsonDocument::fromJson(reply->readAll());
			reply->deleteLater();

			if (status == 200 || status == 201) {
				const QString token = json.object()
					.value(QStringLiteral("sha1")).toString();
				if (token.isEmpty()) {
					if (done) done(false,
						tr("伺服器未回傳 token。"));
				} else if (done) {
					done(true, token);
				}
			} else {
				const QString detail = json.object()
					.value(QStringLiteral("message"))
					.toString();
				if (done) done(false, tr("產生 token 失敗:"
					"http %1%2").arg(status)
					.arg(detail.isEmpty() ? QString()
							      : ':' + detail));
			}
		});
	});
}

void PdmService::listRepositories(Callback done)
{
	// limit=50:試點階段足夠;超過時要改分頁(注意事項見開發計畫)
	get(QStringLiteral("/repos/search?limit=50&archived=false"),
	    std::move(done));
}

void PdmService::listOpenPullRequests(const QString &repo_full_name,
				      Callback done)
{
	get(QStringLiteral("/repos/%1/pulls?state=open&limit=50")
		.arg(repo_full_name), std::move(done));
}

void PdmService::createPullRequest(const QString &repo_full_name,
				   const QString &head_branch,
				   const QString &base_branch,
				   const QString &title, const QString &body,
				   Callback done)
{
	post(QStringLiteral("/repos/%1/pulls").arg(repo_full_name),
	     {{QStringLiteral("head"), head_branch},
	      {QStringLiteral("base"), base_branch},
	      {QStringLiteral("title"), title},
	      {QStringLiteral("body"), body}},
	     std::move(done));
}

void PdmService::submitReview(const QString &repo_full_name, int pr_index,
			      const QString &event, const QString &body,
			      Callback done)
{
	post(QStringLiteral("/repos/%1/pulls/%2/reviews")
		.arg(repo_full_name).arg(pr_index),
	     {{QStringLiteral("event"), event},
	      {QStringLiteral("body"), body}},
	     std::move(done));
}

void PdmService::mergePullRequest(const QString &repo_full_name, int pr_index,
				  Callback done, int max_retries)
{
	post(QStringLiteral("/repos/%1/pulls/%2/merge")
		.arg(repo_full_name).arg(pr_index),
	     {{QStringLiteral("Do"), QStringLiteral("merge")},
	      {QStringLiteral("delete_branch_after_merge"), true}},
	     [this, repo_full_name, pr_index, done, max_retries]
	     (const Reply &reply) {
		if (!reply.ok && max_retries > 0) {
			// §4 驗證:核准後可合併狀態有短暫延遲,等 1 秒重試
			QTimer::singleShot(1000, this,
				[this, repo_full_name, pr_index, done,
				 max_retries]() {
					mergePullRequest(repo_full_name,
						pr_index, done,
						max_retries - 1);
				});
			return;
		}
		if (done) done(reply);
	});
}

void PdmService::createRelease(const QString &repo_full_name,
			       const QString &tag_name,
			       const QString &target_commitish,
			       const QString &title, const QString &body,
			       Callback done)
{
	post(QStringLiteral("/repos/%1/releases").arg(repo_full_name),
	     {{QStringLiteral("tag_name"), tag_name},
	      {QStringLiteral("target_commitish"), target_commitish},
	      {QStringLiteral("name"), title},
	      {QStringLiteral("body"), body}},
	     std::move(done));
}

void PdmService::uploadReleaseAsset(const QString &repo_full_name,
				    qint64 release_id,
				    const QString &file_path, Callback done)
{
	QFile file(file_path);
	if (!file.open(QIODevice::ReadOnly)) {
		Reply reply;
		reply.error = tr("無法讀取附件:%1").arg(file_path);
		if (done) done(reply);
		return;
	}
	const QByteArray data = file.readAll();
	const QString name = QFileInfo(file_path).fileName();

	// Gitea 附件端點吃 multipart/form-data 的 "attachment" 欄位
	const QByteArray boundary = "----QetPdmBoundary7MA4YWxkTrZu0gW";
	QByteArray payload;
	payload += "--" + boundary + "\r\n";
	payload += "Content-Disposition: form-data; name=\"attachment\"; "
		   "filename=\"" + name.toUtf8() + "\"\r\n";
	payload += "Content-Type: application/pdf\r\n\r\n";
	payload += data;
	payload += "\r\n--" + boundary + "--\r\n";

	request("POST",
		QStringLiteral("/repos/%1/releases/%2/assets?name=%3")
			.arg(repo_full_name).arg(release_id)
			.arg(QString::fromUtf8(QUrl::toPercentEncoding(name))),
		payload,
		"multipart/form-data; boundary=" + boundary,
		std::move(done));
}
