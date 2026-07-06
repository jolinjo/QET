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
#include "cli/qetcli.h"
#include "machine_info.h"
#include "qet.h"
#include "qetapp.h"
#include "singleapplication.h"
#include "utils/macosxopenevent.h"
#include "utils/qetsettings.h"

#include <QMutex>
#include <QStyleFactory>
#include <QtConcurrentRun>
#include <QSettings>
#include <QFileInfo>

/**
	@brief myMessageOutput
	for debugging
	@param type : the messages that can be sent to a message handler
	@param context : were? wat?
	@param msg : Message
*/
void myMessageOutput(QtMsgType type,
			 const QMessageLogContext &context,
			 const QString &msg)
{

	QString txt=QTime::currentTime().toString("hh:mm:ss.zzz");
	QByteArray dbs =txt.toLocal8Bit();
	QByteArray localMsg = msg.toLocal8Bit();
	const char *file = context.file ? context.file : "";
	const char *function = context.function ? context.function : "";

	switch (type) {
	case QtDebugMsg:
		fprintf(stderr,
			"%s Debug: %s (%s:%u, %s)\n",
			dbs.constData(),
			localMsg.constData(),
			file,
			context.line,
			function);
		/* les messages de debug ne vont pas dans le fichier journal :
		 * certains chemins (collage inter-projets...) en emettent des
		 * milliers et l'ecriture fichier gelait l'interface */
		return;
	case QtInfoMsg:
		fprintf(stderr,
			"%s Info: %s \n",
			dbs.constData(),
			localMsg.constData());
		txt+=" Info: ";
		break;
	case QtWarningMsg:
		fprintf(stderr,
			"%s Warning: %s (%s:%u, %s)\n",
			dbs.constData(),
			localMsg.constData(),
			file, context.line,
			function);
		txt+=" Warning: ";
		break;
	case QtCriticalMsg:
		fprintf(stderr,
			"%s Critical: %s (%s:%u, %s)\n",
			dbs.constData(),
			localMsg.constData(),
			file,
			context.line,
			function);
		txt+=" Critical: ";
		break;
	case QtFatalMsg:
		fprintf(stderr,
			"%s Fatal: %s (%s:%u, %s)\n",
			dbs.constData(),
			localMsg.constData(),
			file,
			context.line,
			function);
		txt+=" Fatal: ";
		break;
	default:
		fprintf(stderr,
			"%s Unknown: %s (%s:%u, %s)\n",
			dbs.constData(),
			localMsg.constData(),
			file,
			context.line,
			function);
		txt+=" Unknown: ";
	}
	txt+= msg;
	if(type==QtInfoMsg){
		txt+=" \n";
	} else {
		txt+= " (";
		txt+= context.file ? context.file : "";
		txt+= ":";
		txt+=QString::number(context.line ? context.line :0);
		txt+= ", ";
		txt+= context.function ? context.function : "";
		txt+=")\n";
	}
	/* fichier journal garde ouvert (l'ouverture/fermeture a chaque
	 * message coutait tres cher) ; mutex : le gestionnaire peut etre
	 * appele depuis les threads de travail */
	static QMutex log_mutex;
	QMutexLocker locker(&log_mutex);
	static QFile outFile;
	static QString log_date;
	const QString date = QDate::currentDate().toString("yyyyMMdd");
	if (!outFile.isOpen() || log_date != date) {
		outFile.close();
		log_date = date;
		outFile.setFileName(QETApp::dataDir() % "/" % date % ".log");
		outFile.open(QIODevice::WriteOnly | QIODevice::Append);
	}
	if (outFile.isOpen()) {
		QTextStream ts(&outFile);
		ts << txt;
		ts.flush();
	}
}

/**
	@brief delete_old_log_files
	delete old log files
	@param days : max days old
*/
void delete_old_log_files(int days)
{
	const QDate today = QDate::currentDate();
	const QString path = QETApp::dataDir() % "/";

	QString filter("%1%1%1%1%1%1%1%1.log"); // pattern
	filter = filter.arg("[0123456789]"); // valid characters

	Q_FOREACH (auto fileInfo,
		   QDir(path).entryInfoList(
			   QStringList(filter),
			   QDir::Files))
	{
		if (fileInfo.lastRead().date().daysTo(today) > days)
		{
			QString filepath = fileInfo.absoluteFilePath();
			QDir deletefile;
			deletefile.setPath(filepath);
			deletefile.remove(filepath);
			qDebug() << "File " % filepath % " is deleted!";
		}
	}
}

/**
	@brief main
	Main function of QElectroTech
	@param argc : number of parameters
	\~French number of paramètres
	\~ @param argv : parameters
	\~French paramètres
	\~ @return exit code
*/
int main(int argc, char **argv)
{
	// headless command-line mode: no GUI, no single-instance logic
	if (QetCli::isCliInvocation(argc, argv))
		return QetCli::run(argc, argv);

	// before creating Application:
	// export environment-variable "QT_HASH_SEED" with value "0" to
	// disable radomisation for hashes in order to obtain "clean" XML-diffs:
	qputenv("QT_HASH_SEED", "0");
	//Some setup, notably to use with QSetting.
	QCoreApplication::setOrganizationName("QElectroTech");
	QCoreApplication::setOrganizationDomain("qelectrotech.org");
	QCoreApplication::setApplicationName("QElectroTech");

	// Portable / zero-trace mode: when --config-dir=DIR is given, store the
	// application settings in an INI file inside DIR instead of the native
	// backend (Windows registry / macOS plist). Combined with --data-dir this
	// leaves no trace outside the application folder. Installed builds (no
	// --config-dir) keep their default native-settings behaviour unchanged.
	// Must run before the first QSettings access (e.g. QetSettings below).
	for (int i = 1; i < argc; ++i) {
		const QString option = QString::fromLocal8Bit(argv[i]);
		const QString cd_arg = QStringLiteral("--config-dir=");
		if (option.startsWith(cd_arg)) {
			const QString dir = option.mid(cd_arg.length());
			if (!dir.isEmpty()) {
				QSettings::setDefaultFormat(QSettings::IniFormat);
				QSettings::setPath(
					QSettings::IniFormat, QSettings::UserScope, dir);
			}
			break;
		}
	}
	//Creation and execution of the application
	//HighDPI

	qputenv("QT_ENABLE_HIGHDPI_SCALING", "1");
	QGuiApplication::setHighDpiScaleFactorRoundingPolicy(QetSettings::hdpiScaleFactorRoundingPolicy());


#ifdef Q_OS_MACOS
	/* macOS：SingleApplication 的共享記憶體鎖不可靠（POSIX segment 遇
	 * crash/kill 會殘留；SysV 在由 LaunchServices 啟動時被拒絕），會讓
	 * app 啟動即退出。macOS 的 LaunchServices 本身就保證 GUI 單一實例，
	 * 檔案開啟也走 Apple Events（MacOSXOpenEvent），故直接用 QApplication。 */
	QApplication app(argc, argv);
	//Handle the opening of QET when user double click on a .qet .elmt .tbt file
	//or drop these same files to the QET icon of the dock
	MacOSXOpenEvent open_event;
	app.installEventFilter(&open_event);
	app.setStyle(QStyleFactory::create("Fusion"));

	QETApp qetapp;
	QETApp::instance()->installEventFilter(&qetapp);
#else
	SingleApplication app(argc, argv, true);

	if (app.isSecondary())
	{
		QStringList arg_list = app.arguments();
		//Remove the first argument, it's the binary file
		arg_list.takeFirst();
		QETArguments qetarg(arg_list);
		QString message = "launched-with-args: " + QET::joinWithSpaces(
					QStringList(qetarg.arguments()));
		app.sendMessage(message.toUtf8());
		return 0;
	}

	QETApp qetapp;
	QETApp::instance()->installEventFilter(&qetapp);
	QObject::connect(&app, &SingleApplication::receivedMessage,
			 &qetapp, &QETApp::receiveMessage);
#endif

	QtConcurrent::run([=]()
	{
		// for debugging
		qInstallMessageHandler(myMessageOutput);
		qInfo("Start-up");
		// delete old log files of max 7 days old.
		delete_old_log_files(7);
		MachineInfo::instance()->send_info_to_debug();
	});
	return app.exec();
}

