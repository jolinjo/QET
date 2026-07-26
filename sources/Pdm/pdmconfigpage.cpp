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
#include "pdmconfigpage.h"

#include "pdmservice.h"
#include "pdmsettings.h"
#include "../qeticons.h"

#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

PdmConfigPage::PdmConfigPage(QWidget *parent) :
	ConfigPage(parent)
{
	auto *layout = new QVBoxLayout(this);
	auto *form = new QFormLayout();
	layout->addLayout(form);

	// 顯示原始設定值(serverUrl() 是探測後的執行期位址,存回會蓋掉內網設定)
	m_server_edit = new QLineEdit(PdmSettings::configuredServerUrl(), this);
	m_server_edit->setPlaceholderText(QStringLiteral("http://192.168.1.148:3000"));
	form->addRow(tr("Gitea 伺服器:"), m_server_edit);

	// 帳密登入:填帳號密碼後按「登入」自動產生 token,免去手動貼上。
	m_user_edit = new QLineEdit(PdmSettings::username(), this);
	m_user_edit->setPlaceholderText(tr("帳號或 email"));
	form->addRow(tr("帳號:"), m_user_edit);

	auto *pass_row = new QHBoxLayout();
	m_pass_edit = new QLineEdit(this);
	m_pass_edit->setEchoMode(QLineEdit::Password);
	m_pass_edit->setPlaceholderText(tr("登入後自動取得 token,密碼不會儲存"));
	auto *login_button = new QPushButton(tr("登入取得 token"), this);
	pass_row->addWidget(m_pass_edit, 1);
	pass_row->addWidget(login_button);
	form->addRow(tr("密碼:"), pass_row);

	m_token_edit = new QLineEdit(PdmSettings::token(), this);
	m_token_edit->setEchoMode(QLineEdit::Password);
	form->addRow(tr("Access token:"), m_token_edit);

	m_repo_edit = new QLineEdit(PdmSettings::repo(), this);
	m_repo_edit->setPlaceholderText(
		QStringLiteral("HC-Git/HC_Electrical-Schematics"));
	form->addRow(tr("圖庫 Repo:"), m_repo_edit);

	auto *workroot_row = new QHBoxLayout();
	m_workroot_edit = new QLineEdit(PdmSettings::workRoot(), this);
	auto *browse = new QPushButton(tr("瀏覽…"), this);
	workroot_row->addWidget(m_workroot_edit, 1);
	workroot_row->addWidget(browse);
	form->addRow(tr("本機工作區:"), workroot_row);

	auto *test_row = new QHBoxLayout();
	auto *test_button = new QPushButton(tr("驗證連線"), this);
	m_test_result = new QLabel(this);
	m_test_result->setWordWrap(true);
	test_row->addWidget(test_button);
	test_row->addWidget(m_test_result, 1);
	layout->addLayout(test_row);

	auto *hint = new QLabel(
		tr("token 可在 Gitea 網頁「設定→應用程式→產生 token」建立,"
		   "權限至少需要 repository(讀寫)。"), this);
	hint->setWordWrap(true);
	layout->addWidget(hint);
	layout->addStretch();

	connect(browse, &QPushButton::clicked, this, [this]() {
		const QString dir = QFileDialog::getExistingDirectory(
			this, tr("選擇本機工作區"), m_workroot_edit->text());
		if (!dir.isEmpty()) m_workroot_edit->setText(dir);
	});
	connect(login_button, &QPushButton::clicked,
		this, &PdmConfigPage::loginWithPassword);
	connect(test_button, &QPushButton::clicked,
		this, &PdmConfigPage::testConnection);
}

void PdmConfigPage::loginWithPassword()
{
	// 伺服器網址要先有,才能連去產 token
	PdmSettings::setServerUrl(m_server_edit->text());
	if (PdmSettings::configuredServerUrl().isEmpty()) {
		m_test_result->setText(tr("✗ 請先填 Gitea 伺服器網址。"));
		return;
	}

	const QString username = m_user_edit->text().trimmed();
	const QString password = m_pass_edit->text();
	if (username.isEmpty() || password.isEmpty()) {
		m_test_result->setText(tr("✗ 帳號與密碼皆不可空白。"));
		return;
	}

	m_test_result->setText(tr("登入中…"));
	auto *service = new PdmService(this);
	service->createTokenWithPassword(username, password,
		[this, service, username](bool ok, const QString &token_or_error) {
			if (ok) {
				m_token_edit->setText(token_or_error);
				PdmSettings::setToken(token_or_error);
				PdmSettings::setUsername(username);
				// 密碼用完即清,不留在畫面也不儲存
				m_pass_edit->clear();
				m_test_result->setText(
					tr("✓ 登入成功,已自動產生並填入 token。"));
			} else {
				m_test_result->setText(
					tr("✗ %1").arg(token_or_error));
			}
			service->deleteLater();
		});
}

void PdmConfigPage::applyConf()
{
	PdmSettings::setServerUrl(m_server_edit->text());
	PdmSettings::setUsername(m_user_edit->text().trimmed());
	PdmSettings::setToken(m_token_edit->text().trimmed());
	PdmSettings::setRepo(m_repo_edit->text());
	PdmSettings::setWorkRoot(m_workroot_edit->text());
}

void PdmConfigPage::testConnection()
{
	// 先暫存目前輸入值再測,使用者不用先按「確定」
	applyConf();
	m_test_result->setText(tr("測試中…"));
	auto *service = new PdmService(this);
	service->verifyConnection([this, service](bool ok, const QString &result) {
		m_test_result->setText(ok ? tr("✓ 連線成功,帳號:%1").arg(result)
					  : tr("✗ %1").arg(result));
		service->deleteLater();
	});
}

QString PdmConfigPage::title() const
{
	return tr("圖檔管理");
}

QIcon PdmConfigPage::icon() const
{
	return QET::Icons::Settings;
}
