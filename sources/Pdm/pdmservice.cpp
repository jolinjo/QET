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

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

PdmService::PdmService(QObject *parent) :
	QObject(parent),
	m_network(new QNetworkAccessManager(this))
{
}

void PdmService::get(const QString &api_path, Callback done)
{
	QNetworkRequest request(QUrl(PdmSettings::serverUrl()
				     + QStringLiteral("/api/v1") + api_path));
	// token 只進 Authorization 標頭,絕不進 URL 或任何 log。
	request.setRawHeader("Authorization",
			     "token " + PdmSettings::token().toUtf8());
	request.setTransferTimeout(15000);

	QNetworkReply *network_reply = m_network->get(request);
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
				reply.error = tr("伺服器回應 http %1")
					.arg(reply.http_status);
			}
			network_reply->deleteLater();
			if (done) done(reply);
		});
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

void PdmService::listRepositories(Callback done)
{
	// limit=50:試點階段足夠;超過時要改分頁(注意事項見開發計畫)
	get(QStringLiteral("/repos/search?limit=50&archived=false"),
	    std::move(done));
}
