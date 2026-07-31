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
#include "pdmgitworker.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTimer>

namespace
{
	// git-over-HTTP 撞到伺服器一時性錯誤(5xx / Windows Gitea git 程序啟動
	// 失敗)時的特徵字串,判定可重試。
	bool isTransientServerError(const QString &out)
	{
		const QString s = out.toLower();
		return s.contains(QLatin1String("error: 500"))
		    || s.contains(QLatin1String("returned error: 50"))  // 500/502/503/504
		    || s.contains(QLatin1String("0xc0000142"))
		    || s.contains(QLatin1String("exit status 0xc"))
		    || s.contains(QLatin1String("502 bad gateway"))
		    || s.contains(QLatin1String("503 service"))
		    || s.contains(QLatin1String("504 gateway"));
	}
}

PdmGitWorker::PdmGitWorker(QObject *parent) :
	QObject(parent)
{
}

void PdmGitWorker::enqueue(const QStringList &arguments,
			   const QString &working_dir,
			   Callback done)
{
	m_queue.enqueue({QString(), arguments, working_dir, std::move(done)});
	if (!m_process) startNext();
}

void PdmGitWorker::enqueueProgram(const QString &program,
				  const QStringList &arguments,
				  const QString &working_dir,
				  Callback done)
{
	m_queue.enqueue({program, arguments, working_dir, std::move(done)});
	if (!m_process) startNext();
}

void PdmGitWorker::cancelPending()
{
	m_queue.clear();
}

void PdmGitWorker::startNext()
{
	// 已有行程在跑就不再啟動:維持嚴格序列化。若 done-callback 一次
	// enqueue 多個 job,第一個會因 m_process 暫為 null 而自動起跑,此時
	// finished lambda 尾端的 startNext() 不能再起第二個,否則 git add 與
	// git commit 會並行 → commit 撞到空索引「nothing to commit」而漏檔。
	if (m_process) return;
	if (m_queue.isEmpty()) {
		emit allFinished();
		return;
	}
	const Job job = m_queue.dequeue();

	auto *process = new QProcess(this);
	m_process = process;
	process->setProcessChannelMode(QProcess::MergedChannels);
	if (!job.working_dir.isEmpty())
		process->setWorkingDirectory(job.working_dir);

	// 讓 `git`／`git lfs …` 找得到隨程式打包的可攜版:插到 PATH 最前,
	// 找不到就回退系統 PATH。
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
#ifdef Q_OS_WIN
	// 免安裝版零依賴(同事機器不裝 git):MinGit 與 git-lfs 隨包附在
	// ../git(git/cmd/git.exe、git/mingw64/bin/git-lfs.exe)。
	const QString git_root = QDir::cleanPath(
		QCoreApplication::applicationDirPath()
		+ QStringLiteral("/../git"));
	if (QFileInfo::exists(git_root + QStringLiteral("/cmd/git.exe"))) {
		env.insert(QStringLiteral("PATH"),
			QDir::toNativeSeparators(git_root
				+ QStringLiteral("/cmd"))
			+ QLatin1Char(';')
			+ QDir::toNativeSeparators(git_root
				+ QStringLiteral("/mingw64/bin"))
			+ QLatin1Char(';')
			+ env.value(QStringLiteral("PATH")));
		process->setProcessEnvironment(env);
	}
#else
	// macOS:git-lfs 打包進 bundle Resources/bin(Finder 啟動的 app PATH
	// 精簡,系統/brew 的 git-lfs 找不到)。系統 git 仍走 /usr/bin。
	const QString bundled_bin = QDir::cleanPath(
		QCoreApplication::applicationDirPath()
		+ QStringLiteral("/../Resources/bin"));
	if (QFileInfo::exists(bundled_bin + QStringLiteral("/git-lfs"))) {
		env.insert(QStringLiteral("PATH"), bundled_bin
			   + QLatin1Char(':')
			   + env.value(QStringLiteral("PATH")));
		process->setProcessEnvironment(env);
	}
#endif

	const bool is_git = job.program.isEmpty();
	const QString program = is_git ? QStringLiteral("git") : job.program;

	// CJK 檔名不轉義、Windows 長路徑;凡 clone/pull 觸發的 LFS 下載
	// 都走同一組環境。
	QStringList args;
	if (is_git) {
		args << "-c" << "core.quotepath=false"
		     << "-c" << "core.longpaths=true";
	}
	args << job.arguments;

	emit stepStarted((is_git ? QStringLiteral("git ") : program + ' ')
			 + job.arguments.join(' '));

	connect(process,
		QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
		this,
		[this, process, job](int exit_code, QProcess::ExitStatus status) {
			Result result;
			result.exit_code = exit_code;
			result.ok = (status == QProcess::NormalExit && exit_code == 0);
			result.output = QString::fromUtf8(process->readAll());
			process->deleteLater();
			m_process = nullptr;
			// 自我修復:git 一次只跑一個行程(佇列序列化),因此撞到的
			// index.lock 一定是先前被中斷的行程留下的 stale lock(例如
			// 操作中途關程式)。移除它並把同一個工作重試一次。
			if (!result.ok && !job.lock_retried
			    && result.output.contains(QLatin1String("index.lock"))
			    && result.output.contains(QLatin1String("File exists"))) {
				const QRegularExpression re(
					QStringLiteral("'([^']*index\\.lock)'"));
				const auto m = re.match(result.output);
				if (m.hasMatch() && QFile::exists(m.captured(1))
				    && QFile::remove(m.captured(1))) {
					Job retry = job;
					retry.lock_retried = true;
					m_queue.prepend(retry);
					startNext();
					return;
				}
			}
			// 伺服器一時性 5xx(含 Windows Gitea 端 git 程序啟動失敗
			// 0xc0000142):稍等後自動重試幾次,重試用盡才把錯誤丟給
			// 使用者(避免伺服器抖動就打斷流程)。
			if (!result.ok && job.server_retries < 3
			    && isTransientServerError(result.output)) {
				Job retry = job;
				retry.server_retries = job.server_retries + 1;
				QTimer::singleShot(1000, this, [this, retry]() {
					m_queue.prepend(retry);
					if (!m_process) startNext();
				});
				return;
			}
			if (job.done) job.done(result);
			startNext();
		});
	connect(process, &QProcess::errorOccurred, this,
		[this, process, job, program](QProcess::ProcessError) {
			if (process->state() != QProcess::NotRunning) return;
			Result result;
			result.output = tr("無法啟動 %1,請確認已正確安裝。")
				.arg(program);
			process->deleteLater();
			m_process = nullptr;
			if (job.done) job.done(result);
			startNext();
		});

	process->start(program, args);
}
