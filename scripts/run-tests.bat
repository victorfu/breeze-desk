@echo off
setlocal
cd /d "%~dp0\.."
if not exist "build\debug\CMakeCache.txt" call scripts\build.bat || exit /b 1
ctest --test-dir build\debug --output-on-failure %* || exit /b 1
