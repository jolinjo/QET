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
#ifndef PDMGITWORKER_H
#define PDMGITWORKER_H

#include <QObject>
#include <QQueue>
#include <QStringList>

#include <functional>

class QProcess;

/**
	@brief 非同步 git / git-lfs 子行程執行器(PDM 用)。
	一次只跑一個指令,其餘排隊;全程不阻塞 UI 執行緒。
	每一步的輸出以 stepOutput 訊號回報,可直接餵給狀態列。
*/
class PdmGitWorker : public QObject
{
	Q_OBJECT

	public:
		struct Result {
			bool ok = false;
			int exit_code = -1;
			QString output;   ///< stdout+stderr 合併
		};
		using Callback = std::function<void (const Result &)>;

		explicit PdmGitWorker(QObject *parent = nullptr);

		/**
			將一個 git 指令排入佇列。
			@param arguments git 參數(不含 "git" 本身)
			@param working_dir 工作目錄(空字串 = 目前目錄)
			@param done 完成回呼(在主執行緒呼叫)
		*/
		void enqueue(const QStringList &arguments,
			     const QString &working_dir,
			     Callback done);

		/// 同 enqueue,但執行任意程式(發行時呼叫自身 CLI 產 PDF 用)
		void enqueueProgram(const QString &program,
				    const QStringList &arguments,
				    const QString &working_dir,
				    Callback done);

		bool busy() const { return m_process != nullptr; }
		void cancelPending();

	signals:
		/// 目前執行中指令的說明(給 UI 狀態列)
		void stepStarted(const QString &description);
		void allFinished();

	private:
		void startNext();

		struct Job {
			QString program;   ///< 空 = git
			QStringList arguments;
			QString working_dir;
			Callback done;
			bool lock_retried = false;  ///< 已為 stale index.lock 重試過一次
			int server_retries = 0;     ///< 已為伺服器 5xx 重試的次數
		};
		QQueue<Job> m_queue;
		QProcess *m_process = nullptr;
};

#endif // PDMGITWORKER_H
