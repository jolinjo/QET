@echo off
call "C:\Users\JasonLin\Documents\ClaudeCode\QET\winenv.bat"
cd /d "C:\Users\JasonLin\Documents\ClaudeCode\QET"
cmake --build build --parallel
echo BUILD_EXIT=%ERRORLEVEL%
