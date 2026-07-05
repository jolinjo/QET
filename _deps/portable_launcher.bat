@echo off
rem QElectroTech portable launcher (fully self-contained, zero-trace).
rem Copy this whole folder anywhere (incl. USB) and double-click to run.
rem All state stays inside this folder:
rem   config\QElectroTech\QElectroTech.ini  - preferences (this fork's patch:
rem                                           --config-dir switches QSettings to
rem                                           an INI file instead of the registry)
rem   data\                                 - user collections, cache, log
rem   elements\  titleblocks\               - bundled common element / title-block library
rem overrideConfigDir/DataDir require the target folders to exist, so mkdir first.
setlocal
set "HERE=%~dp0"
cd /d "%HERE%"
if not exist "%HERE%config" mkdir "%HERE%config"
if not exist "%HERE%data"   mkdir "%HERE%data"
start "" "%HERE%qelectrotech.exe" --config-dir="%HERE%config" --data-dir="%HERE%data" --common-elements-dir="%HERE%elements" --common-tbt-dir="%HERE%titleblocks" %*
endlocal
