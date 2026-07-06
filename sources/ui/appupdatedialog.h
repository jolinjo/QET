/*
	Copyright 2006-2026 The QElectroTech Team
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
#ifndef APPUPDATEDIALOG_H
#define APPUPDATEDIALOG_H

#include <QDialog>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

/**
	@brief The AppUpdateDialog class
	OTA update of the application itself from the intranet git release
	repository (mac-stable branch): list the published versions (tags),
	switch to any of them -- newer or older -- then relaunch.
*/
class AppUpdateDialog : public QDialog
{
	Q_OBJECT

	public:
	explicit AppUpdateDialog(QWidget *parent = nullptr);

	private:
	void refreshVersionList();
	void applySelectedVersion();
	QString bundlePath() const;

	QLineEdit *m_url;
	QListWidget *m_versions;
	QPushButton *m_refresh;
	QPushButton *m_apply;
	QLabel *m_status;
};

#endif // APPUPDATEDIALOG_H
