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
#include "appupdatedialog.h"

#include "../qetapp.h"
#include "../qetversion.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>
#include <QVersionNumber>

namespace
{
	//依平台決定發佈通道:release repo 的分支、tag 前綴與更新紀錄檔
#ifdef Q_OS_WIN
	const QLatin1String TAG_PREFIX("win-v");
	const QLatin1String OTA_BRANCH("win-stable");
	const QLatin1String OTA_CHANGELOG("CHANGELOG-win.md");
#else
	const QLatin1String TAG_PREFIX("mac-v");
	const QLatin1String OTA_BRANCH("mac-stable");
	const QLatin1String OTA_CHANGELOG("CHANGELOG-mac.md");
#endif

	bool run_process(const QString &program, const QStringList &arguments,
			 QString *output)
	{
		QProcess process;
		process.setProcessChannelMode(QProcess::MergedChannels);
		process.start(program, arguments);
		if (!process.waitForStarted()) {
			if (output) {
				*output = program + QStringLiteral(" : ")
					  + process.errorString();
			}
			return false;
		}
		QEventLoop loop;
		QObject::connect(
			&process,
			QOverload<int, QProcess::ExitStatus>::of(
				&QProcess::finished),
			&loop, &QEventLoop::quit);
		loop.exec();
		if (output) {
			*output = QString::fromUtf8(process.readAll());
		}
		return process.exitStatus() == QProcess::NormalExit
		       && process.exitCode() == 0;
	}
}

AppUpdateDialog::AppUpdateDialog(QWidget *parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Mise à jour de l'application", "window title"));

	QSettings settings;
	m_url = new QLineEdit(
		settings.value(QStringLiteral("ota/repo-url"),
			       QStringLiteral("http://192.168.1.148:3000/HC-Git"
					      "/QET-release")).toString(),
		this);

	m_versions = new QListWidget(this);
	m_versions->setMinimumSize(460, 140);

	m_notes = new QTextBrowser(this);
	m_notes->setMinimumHeight(180);
	m_notes->setOpenExternalLinks(false);

	m_refresh = new QPushButton(tr("Actualiser"), this);
	connect(m_refresh, &QPushButton::clicked,
		this, &AppUpdateDialog::refreshVersionList);

	m_apply = new QPushButton(
		tr("Passer à la version sélectionnée"), this);
	m_apply->setEnabled(false);
	connect(m_apply, &QPushButton::clicked,
		this, &AppUpdateDialog::applySelectedVersion);
	connect(m_versions, &QListWidget::itemSelectionChanged, this,
		[this]() {
		m_apply->setEnabled(!m_versions->selectedItems().isEmpty());
	});

	m_status = new QLabel(this);
	m_status->setWordWrap(true);

	auto *url_row = new QHBoxLayout();
	url_row->addWidget(new QLabel(tr("Dépôt de mise à jour :"), this));
	url_row->addWidget(m_url, 1);
	url_row->addWidget(m_refresh);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	buttons->addButton(m_apply, QDialogButtonBox::ActionRole);

	auto *layout = new QVBoxLayout(this);
	layout->addLayout(url_row);
	layout->addWidget(new QLabel(
		tr("Version actuelle : %1").arg(
			QetVersion::displayedVersion()), this));
	layout->addWidget(m_versions);
	layout->addWidget(new QLabel(tr("Nouveautés :"), this));
	layout->addWidget(m_notes);
	layout->addWidget(m_status);
	layout->addWidget(buttons);

	refreshVersionList();
}

/**
	List the published versions (win-v* or mac-v* tags of the release
	repository), newest first.
*/
void AppUpdateDialog::refreshVersionList()
{
	const QString url = m_url->text().trimmed();
	if (url.isEmpty()) return;
	QSettings().setValue(QStringLiteral("ota/repo-url"), url);

	m_versions->clear();
	m_status->setText(tr("Interrogation du dépôt..."));
	QCoreApplication::processEvents();

	QList<QVersionNumber> versions;
#ifdef Q_OS_WIN
	//les postes Windows n'ont pas forcement git : on interroge
	//l'API Gitea (curl est fourni avec Windows 10+)
	const QUrl base(url);
	const QString api = base.scheme() % QStringLiteral("://")
			    % base.authority()
			    % QStringLiteral("/api/v1/repos") % base.path()
			    % QStringLiteral("/tags");
	QString output;
	if (!run_process(QStringLiteral("curl"),
			 { QStringLiteral("-fsS"), QStringLiteral("--max-time"),
			   QStringLiteral("10"), api },
			 &output)) {
		m_status->setText(tr("Échec : %1").arg(output.right(600)));
		return;
	}
	const QJsonArray tags =
		QJsonDocument::fromJson(output.toUtf8()).array();
	for (const QJsonValue &value : tags) {
		const QString name =
			value.toObject().value(QLatin1String("name")).toString();
		if (name.startsWith(TAG_PREFIX)) {
			versions << QVersionNumber::fromString(
				name.mid(TAG_PREFIX.size()));
		}
	}
#else
	QString output;
	if (!run_process(QStringLiteral("git"),
			 { QStringLiteral("ls-remote"), QStringLiteral("--tags"),
			   url },
			 &output)) {
		m_status->setText(tr("Échec : %1").arg(output.right(600)));
		return;
	}

	const QStringList lines = output.split(QChar('\n'));
	for (const QString &line : lines) {
		const int index = line.indexOf(
			QStringLiteral("refs/tags/") + TAG_PREFIX);
		if (index == -1 || line.endsWith(QStringLiteral("^{}"))) {
			continue;
		}
		versions << QVersionNumber::fromString(
			line.mid(index + 10 + TAG_PREFIX.size()));
	}
#endif
	std::sort(versions.begin(), versions.end(),
		  [](const QVersionNumber &a, const QVersionNumber &b) {
		return a > b;
	});

	//version courante d'apres la version affichee (currentVersion()
	//garde la valeur amont pour la compatibilite des fichiers)
	const QVersionNumber current = QVersionNumber::fromString(
		QetVersion::displayedVersion().section(QChar('-'), 0, 0));
	for (int i = 0 ; i < versions.count() ; ++i) {
		const QVersionNumber &version = versions.at(i);
		QString text = QStringLiteral("v") + version.toString();
		if (i == 0) {
			text += QStringLiteral("  —  ")
				+ tr("dernière version");
		}
		if (version == current) {
			text += QStringLiteral("  —  ")
				+ tr("version actuelle");
		}
		auto *item = new QListWidgetItem(text, m_versions);
		item->setData(Qt::UserRole,
			      TAG_PREFIX + version.toString());
	}
	m_status->setText(versions.isEmpty()
		? tr("Aucune version publiée trouvée.")
		: tr("Versions disponibles : %1").arg(versions.count()));

	//journal des modifications publie a cote des binaires (Gitea raw)
	QString notes;
	if (run_process(QStringLiteral("curl"),
			{ QStringLiteral("-fsS"), QStringLiteral("--max-time"),
			  QStringLiteral("5"),
			  url % QStringLiteral("/raw/branch/") % OTA_BRANCH
				  % QChar('/') % OTA_CHANGELOG },
			&notes)) {
		m_notes->setMarkdown(notes);
	} else {
		m_notes->setPlainText(
			tr("(journal des modifications indisponible)"));
	}
}

/**
	@return the installation root of the running application (.app bundle
	on macOS, portable folder -- parent of bin/ -- on Windows), or an
	empty string when the layout is not recognized.
*/
QString AppUpdateDialog::bundlePath() const
{
	QDir dir(QCoreApplication::applicationDirPath());
#ifdef Q_OS_WIN
	if (dir.dirName().compare(QLatin1String("bin"), Qt::CaseInsensitive)) {
		return QString();
	}
	dir.cdUp();               // racine du dossier portable
	return dir.path();
#else
	if (dir.dirName() != QLatin1String("MacOS")) return QString();
	dir.cdUp();               // Contents
	dir.cdUp();               // *.app
	return dir.path().endsWith(QLatin1String(".app")) ? dir.path()
							   : QString();
#endif
}

/**
	Download the selected version and swap the installed files with it,
	then offer to relaunch. Works for downgrades too.
	macOS: blobless git fetch + rsync over the running .app bundle.
	Windows: Gitea zip archive + updater script run after the exit
	(the running exe cannot be overwritten).
*/
void AppUpdateDialog::applySelectedVersion()
{
	const auto selected = m_versions->selectedItems();
	if (selected.isEmpty()) return;
	const QString tag = selected.first()->data(Qt::UserRole).toString();
	const QString url = m_url->text().trimmed();

	const QString bundle = bundlePath();
	if (bundle.isEmpty()) {
#ifdef Q_OS_WIN
		QMessageBox::warning(this, windowTitle(),
			tr("L'application ne tourne pas depuis le dossier"
			   " portable (bin\\QElectroTech.exe) :"
			   " mise à jour impossible."));
#else
		QMessageBox::warning(this, windowTitle(),
			tr("L'application ne tourne pas depuis un bundle"
			   " .app : mise à jour impossible."));
#endif
		return;
	}

	m_apply->setEnabled(false);
#ifdef Q_OS_WIN
	const QString work =
		QETApp::dataDir() % QStringLiteral("/app-ota-win");
	QDir().mkpath(work);
	const QString zip = work % QChar('/') % tag % QStringLiteral(".zip");
	const QString stage = work % QStringLiteral("/stage");
	QDir(stage).removeRecursively();
	QDir().mkpath(stage);

	QString log;
	m_status->setText(tr("Téléchargement de %1...").arg(tag));
	QCoreApplication::processEvents();
	bool ok = run_process(QStringLiteral("curl"),
		{ QStringLiteral("-fsS"), QStringLiteral("-o"), zip,
		  url % QStringLiteral("/archive/") % tag
			  % QStringLiteral(".zip") },
		&log);

	if (ok) {
		m_status->setText(tr("Extraction de %1...").arg(tag));
		QCoreApplication::processEvents();
		//tar (bsdtar) est fourni avec Windows 10+ et extrait les zip
		ok = run_process(QStringLiteral("tar"),
			{ QStringLiteral("-xf"), zip,
			  QStringLiteral("--strip-components=1"),
			  QStringLiteral("-C"), stage }, &log);
	}
	if (ok && !QFileInfo::exists(
			stage % QStringLiteral("/bin/QElectroTech.exe"))) {
		ok = false;
		log = tr("l'archive ne contient pas bin/QElectroTech.exe");
	}

	//l'executable en cours ne peut pas etre remplace sous Windows :
	//un script attend la fermeture (la copie de l'exe ne reussit
	//qu'apres), recopie tout puis relance. robocopy /E ne supprime
	//pas les fichiers que l'utilisateur garde dans le dossier.
	const QString bat = QDir::toNativeSeparators(
		work % QStringLiteral("/apply-update.bat"));
	if (ok) {
		QFile file(bat);
		ok = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
		if (!ok) {
			log = file.errorString();
		} else {
			const QString script = QStringLiteral(
				"@echo off\r\n"
				"echo Mise a jour de QElectroTech (%1)...\r\n"
				":wait\r\n"
				"ping -n 2 127.0.0.1 >nul\r\n"
				"copy /y \"%2\\bin\\QElectroTech.exe\""
					" \"%3\\bin\\QElectroTech.exe\""
					" >nul 2>&1\r\n"
				"if errorlevel 1 goto wait\r\n"
				"robocopy \"%2\" \"%3\""
					" /E /NFL /NDL /NJH /NJS /NP\r\n"
				"if errorlevel 8 (\r\n"
				"  echo ECHEC de la mise a jour.\r\n"
				"  pause\r\n"
				"  exit /b 1\r\n"
				")\r\n"
				"start \"\" \"%3\\bin\\QElectroTech.exe\"\r\n"
				"exit /b 0\r\n")
				.arg(tag,
				     QDir::toNativeSeparators(stage),
				     QDir::toNativeSeparators(bundle));
			file.write(script.toLocal8Bit());
			file.close();
		}
	}

	m_apply->setEnabled(true);
	if (!ok) {
		m_status->setText(QString());
		QMessageBox::warning(this, windowTitle(),
			tr("La mise à jour a échoué :\n%1")
				.arg(log.right(800)));
		return;
	}

	m_status->setText(tr("%1 téléchargée.").arg(tag));
	const auto answer = QMessageBox::question(this, windowTitle(),
		tr("Version %1 téléchargée. L'application va se fermer,"
		   " se mettre à jour puis redémarrer. Continuer ?").arg(tag),
		QMessageBox::Yes | QMessageBox::No);
	if (answer == QMessageBox::Yes) {
		QProcess::startDetached(QStringLiteral("cmd.exe"),
			{ QStringLiteral("/c"), bat });
		QCoreApplication::quit();
	}
#else
	const QString cache =
		QETApp::dataDir() % QStringLiteral("/app-ota-cache");
	QString log;
	bool ok = true;

	m_status->setText(tr("Téléchargement de %1...").arg(tag));
	QCoreApplication::processEvents();
	if (!QFileInfo::exists(cache % QStringLiteral("/.git"))) {
		ok = run_process(QStringLiteral("git"),
			{ QStringLiteral("clone"),
			  QStringLiteral("--filter=blob:none"),
			  QStringLiteral("--single-branch"),
			  QStringLiteral("--branch"),
			  QStringLiteral("mac-stable"),
			  url, cache }, &log);
	} else {
		ok = run_process(QStringLiteral("git"),
			{ QStringLiteral("-C"), cache,
			  QStringLiteral("remote"), QStringLiteral("set-url"),
			  QStringLiteral("origin"), url }, &log)
		     && run_process(QStringLiteral("git"),
			{ QStringLiteral("-C"), cache,
			  QStringLiteral("fetch"), QStringLiteral("--tags"),
			  QStringLiteral("--force"),
			  QStringLiteral("--prune"),
			  QStringLiteral("--prune-tags"),
			  QStringLiteral("origin") }, &log);
	}
	if (ok) {
		ok = run_process(QStringLiteral("git"),
			{ QStringLiteral("-C"), cache,
			  QStringLiteral("-c"),
			  QStringLiteral("advice.detachedHead=false"),
			  QStringLiteral("checkout"), QStringLiteral("-f"),
			  QStringLiteral("tags/") + tag }, &log);
	}
	if (ok && !QFileInfo::exists(
			cache % QStringLiteral("/qelectrotech.app"))) {
		ok = false;
		log = tr("le dépôt ne contient pas qelectrotech.app");
	}

	if (ok) {
		m_status->setText(tr("Installation de %1...").arg(tag));
		QCoreApplication::processEvents();
		ok = run_process(QStringLiteral("rsync"),
			{ QStringLiteral("-a"), QStringLiteral("--delete"),
			  cache % QStringLiteral("/qelectrotech.app/"),
			  bundle % QChar('/') }, &log);
	}

	m_apply->setEnabled(true);
	if (!ok) {
		m_status->setText(QString());
		QMessageBox::warning(this, windowTitle(),
			tr("La mise à jour a échoué :\n%1\n\nSi le message"
			   " indique un refus d'accès, déplacez l'application"
			   " hors de /Applications (par exemple dans"
			   " ~/Applications) puis réessayez.")
				.arg(log.right(800)));
		return;
	}

	m_status->setText(tr("%1 installée.").arg(tag));
	const auto answer = QMessageBox::question(this, windowTitle(),
		tr("Version %1 installée. Relancer maintenant ?").arg(tag),
		QMessageBox::Yes | QMessageBox::No);
	if (answer == QMessageBox::Yes) {
		QProcess::startDetached(
			QStringLiteral("/bin/sh"),
			{ QStringLiteral("-c"),
			  QStringLiteral("sleep 1; open -n \"%1\"")
				  .arg(bundle) });
		QCoreApplication::quit();
	}
#endif
}
