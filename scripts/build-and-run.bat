@echo off
setlocal
cd /d "%~dp0\.."
set "PROJECT_ROOT=%CD%"
call scripts\setup-msvc.bat || exit /b 1
call scripts\build.bat || exit /b 1
call scripts\deploy-debug.bat || exit /b 1
for /f "tokens=2 delims==" %%A in ('cmake -N -LA build\debug ^| findstr /b "BREEZEDESK_DEBUG_EXECUTABLE_NAME:STRING="') do set APP_NAME=%%A
if not defined APP_NAME (
  echo Unable to read BREEZEDESK_DEBUG_EXECUTABLE_NAME from the CMake cache. 1>&2
  exit /b 1
)
set "APP_PATH=%PROJECT_ROOT%\build\debug\%APP_NAME%.exe"
"%APP_PATH%" %*
