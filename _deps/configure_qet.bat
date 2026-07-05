@echo off
call "C:\Users\JasonLin\Documents\ClaudeCode\QET\winenv.bat"
cd /d "C:\Users\JasonLin\Documents\ClaudeCode\QET"
set "SQ=C:\Users\JasonLin\Documents\ClaudeCode\QET\_deps\sqlite\sqlite-amalgamation-3530300"
cmake -G Ninja -S . -B build ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DFETCHCONTENT_UPDATES_DISCONNECTED=ON ^
  -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON ^
  -DBUILD_KF6=ON ^
  -DBUILD_PUGIXML=ON ^
  -DQET_EXAMPLES_PATH=examples/ ^
  -DQET_ICONS_PATH=share/icons/ ^
  -DQET_APPDATA_PATH=share/metainfo/ ^
  -DSQLite3_INCLUDE_DIR="%SQ%" ^
  -DSQLite3_LIBRARY="%SQ%\sqlite3.lib"
echo CONFIGURE_EXIT=%ERRORLEVEL%
