@echo off
call "C:\Users\JasonLin\Documents\ClaudeCode\QET\winenv.bat"
cd /d "C:\Users\JasonLin\Documents\ClaudeCode\QET\_deps\ecm-src"
cmake -G Ninja -S . -B build -DCMAKE_INSTALL_PREFIX="C:\Qt\qet-deps" -DBUILD_TESTING=OFF
cmake --build build --target install
echo ECM_EXIT=%ERRORLEVEL%
