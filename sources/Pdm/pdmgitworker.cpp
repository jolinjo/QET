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

#include <QProcess>

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
