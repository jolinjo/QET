@echo off
rem QET Windows build environment: MSVC + Qt6 + bundled CMake/Ninja
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set "QTDIR=C:\Qt\6.8.3\msvc2022_64"
set "QET_DEPS=C:\Qt\qet-deps"
set "CMAKEDIR=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
set "NINJADIR=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
set "SCCACHEDIR=C:\tools\sccache"
set "PATH=%SCCACHEDIR%;%CMAKEDIR%;%NINJADIR%;%QTDIR%\bin;%QET_DEPS%\bin;%PATH%"
set "CMAKE_PREFIX_PATH=%QTDIR%;%QET_DEPS%"
rem sccache compiler cache: speeds up reconfigure / branch switch / full rebuild
set "SCCACHE_DIR=C:\tools\sccache\cache"
set "SCCACHE_CACHE_SIZE=15G"
