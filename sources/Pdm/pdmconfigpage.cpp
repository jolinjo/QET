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

	m_server_edit = new QLineEdit(PdmSettings::serverUrl(), this);
	m_server_edit->setPlaceholderText(QStringLiteral("http://hc-server:3000"));
	form->addRow(tr("Gitea 伺服器:"), m_server_edit);

	m_token_edit = new QLineEdit(PdmSettings::token(), this);
	m_token_edit->setEchoMode(QLineEdit::Password);
	form->addRow(tr("Access token:"), m_token_edit);

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
	connect(test_button, &QPushButton::clicked,
		this, &PdmConfigPage::testConnection);
}

void PdmConfigPage::applyConf()
{
	PdmSettings::setServerUrl(m_server_edit->text());
	PdmSettings::setToken(m_token_edit->text().trimmed());
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
