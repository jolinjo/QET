/*
	Copyright 2006-2026 The QElectroTech Team
	This file is part of QElectroTech.

	QElectroTech is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	QElectroTech is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with QElectroTech. If not, see <http://www.gnu.org/licenses/>.
*/
#include "macosxopenevent.h"

#include "../qetapp.h"
#include "../qetarguments.h"
#include "singleapplication.h"

#include <QFileOpenEvent>

/**
	@brief MacOSXOpenEvent::MacOSXOpenEvent
	@param parent
*/
MacOSXOpenEvent::MacOSXOpenEvent(QObject *parent) :
	QObject(parent)
{}

/**
	@brief MacOSXOpenEvent::eventFilter
	@param watched
	@param event
	@return bool
*/
bool MacOSXOpenEvent::eventFilter(QObject *watched, QEvent *event)
{
	if (event->type() == QEvent::FileOpen)
	{
		QFileOpenEvent *open_event = static_cast<QFileOpenEvent*>(event);
		/* sur macOS l'application est un simple QApplication (voir
		 * main.cpp) : le dynamic_cast echouait et l'appel dereferencait
		 * un pointeur nul a chaque evenement d'ouverture ; dans ce cas
		 * ouvrir le fichier directement dans ce processus */
		if (SingleApplication *app =
				dynamic_cast<SingleApplication *>(watched)) {
			QString message = "launched-with-args: "
					  + open_event->file();
			app->sendMessage(message.toUtf8());
		} else if (QETApp *qetapp = QETApp::instance()) {
			qetapp->openFiles(QETArguments(
				QStringList{open_event->file()}));
		}
		return true;
	}
	return false;
}
