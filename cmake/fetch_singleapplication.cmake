# Copyright 2006-2026 The QElectroTech Team
# This file is part of QElectroTech.
#
# QElectroTech is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# QElectroTech is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with QElectroTech. If not, see <http://www.gnu.org/licenses/>.

message(" - fetch_singleapplication")

set(QAPPLICATION_CLASS QApplication)

Include(FetchContent)

FetchContent_Declare(
  SingleApplication
  GIT_REPOSITORY https://github.com/itay-grudev/SingleApplication.git
  GIT_TAG        v3.5.4)

FetchContent_MakeAvailable(SingleApplication)

# QET macOS fix: force SystemV shared memory for the single-instance lock.
# On macOS, Qt's QSharedMemory defaults to POSIX shm, which is NOT released
# when a process exits uncleanly (crash / SIGKILL / QCommandLineParser's
# ::exit()). The orphaned segment then makes every later launch believe another
# instance is already running, so the app silently exits at start-up.
# SystemV segments are auto-detached by the kernel on process death, which lets
# SingleApplication's built-in Unix crash-recovery reclaim the primary slot.
if(APPLE)
  set(_qet_sa_cpp "${singleapplication_SOURCE_DIR}/singleapplication.cpp")
  file(READ "${_qet_sa_cpp}" _qet_sa_content)
  string(REPLACE
    "QNativeIpcKey( d->blockServerName )"
    "QNativeIpcKey( d->blockServerName, QNativeIpcKey::Type::SystemV )"
    _qet_sa_content "${_qet_sa_content}")
  file(WRITE "${_qet_sa_cpp}" "${_qet_sa_content}")
  message(STATUS " - QET: patched SingleApplication to use SystemV shared memory")
endif()
