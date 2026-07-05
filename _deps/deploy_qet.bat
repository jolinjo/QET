@echo off
call "C:\Users\JasonLin\Documents\ClaudeCode\QET\winenv.bat"
cd /d "C:\Users\JasonLin\Documents\ClaudeCode\QET\build"
windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw qelectrotech.exe
echo DEPLOY_EXIT=%ERRORLEVEL%
