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
#ifndef PDMCONFIGPAGE_H
#define PDMCONFIGPAGE_H

#include "configpage.h"

class QLabel;
class QLineEdit;

/**
	@brief 偏好設定「圖檔管理」頁:Gitea 伺服器、token、本機工作區。
*/
class PdmConfigPage : public ConfigPage
{
	Q_OBJECT

	public:
		explicit PdmConfigPage(QWidget *parent = nullptr);

		void applyConf() override;
		QString title() const override;
		QIcon icon() const override;

	private:
		void testConnection();

		QLineEdit *m_server_edit = nullptr,
			  *m_token_edit = nullptr,
			  *m_workroot_edit = nullptr;
		QLabel *m_test_result = nullptr;
};

#endif // PDMCONFIGPAGE_H
