@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
set "PROJECT_ROOT=%CD%"
call "%PROJECT_ROOT%\scripts\setup-msvc.bat" || exit /b 1
set "BUILD_DIR=%~1"
if not defined BUILD_DIR set "BUILD_DIR=%PROJECT_ROOT%\build\debug"
for %%I in ("%BUILD_DIR%") do set "BUILD_DIR=%%~fI"
set "CACHE_FILE=%BUILD_DIR%\CMakeCache.txt"
if not exist "%CACHE_FILE%" (
  echo The Debug CMake cache is missing. Run scripts\build.bat first. 1>&2
  exit /b 1
)

for /f "tokens=2 delims==" %%A in ('findstr /b "BREEZEDESK_DEBUG_EXECUTABLE_NAME:STRING=" "%CACHE_FILE%"') do set "APP_NAME=%%A"
for /f "tokens=2 delims==" %%A in ('findstr /b "BREEZEDESK_WORKER_EXECUTABLE_NAME:STRING=" "%CACHE_FILE%"') do set "WORKER_NAME=%%A"
for /f "tokens=2 delims==" %%A in ('findstr /b "BREEZEDESK_CLI_EXECUTABLE_NAME:STRING=" "%CACHE_FILE%"') do set "CLI_NAME=%%A"
if not defined APP_NAME (
  echo Unable to read BREEZEDESK_DEBUG_EXECUTABLE_NAME from the CMake cache. 1>&2
  exit /b 1
)
if not defined WORKER_NAME (
  echo Unable to read BREEZEDESK_WORKER_EXECUTABLE_NAME from the CMake cache. 1>&2
  exit /b 1
)
if not defined CLI_NAME (
  echo Unable to read BREEZEDESK_CLI_EXECUTABLE_NAME from the CMake cache. 1>&2
  exit /b 1
)
set "APP_PATH=%BUILD_DIR%\%APP_NAME%.exe"
set "WORKER_PATH=%BUILD_DIR%\%WORKER_NAME%.exe"
set "CLI_PATH=%BUILD_DIR%\%CLI_NAME%.exe"
for %%F in ("%APP_PATH%" "%WORKER_PATH%" "%CLI_PATH%") do if not exist "%%~F" (
  echo Expected Debug executable is missing: %%~F 1>&2
  exit /b 1
)

set "WINDEPLOYQT=%BREEZEDESK_WINDEPLOYQT%"
if defined WINDEPLOYQT if not exist "%WINDEPLOYQT%" (
  echo BREEZEDESK_WINDEPLOYQT does not exist: %WINDEPLOYQT% 1>&2
  exit /b 1
)
if not defined WINDEPLOYQT for /f "tokens=2 delims==" %%A in ('findstr /b "Qt6_DIR:PATH=" "%CACHE_FILE%"') do set "QT6_CMAKE_DIR=%%A"
if not defined WINDEPLOYQT if defined QT6_CMAKE_DIR for %%I in ("%QT6_CMAKE_DIR%\..\..\..") do set "WINDEPLOYQT=%%~fI\bin\windeployqt.exe"
if not exist "%WINDEPLOYQT%" set "WINDEPLOYQT="
for /f "delims=" %%I in ('where windeployqt.exe 2^>nul') do if not defined WINDEPLOYQT set "WINDEPLOYQT=%%I"
if not exist "%WINDEPLOYQT%" (
  echo Unable to locate the matching windeployqt.exe from BREEZEDESK_WINDEPLOYQT, Qt6_DIR, or PATH. 1>&2
  exit /b 1
)
for %%I in ("%WINDEPLOYQT%") do set "QT_BIN=%%~dpI"
set "QT_TEST_DLL=%QT_BIN%Qt6Testd.dll"
if not exist "%QT_TEST_DLL%" (
  echo The matching Qt Debug test runtime is missing: %QT_TEST_DLL% 1>&2
  exit /b 1
)

"%WINDEPLOYQT%" --debug --force --compiler-runtime --verbose 0 --qmldir "%PROJECT_ROOT%\src\qml" --translations en,zh_TW "%APP_PATH%" "%CLI_PATH%" "%WORKER_PATH%" || exit /b 1
copy /y "%QT_TEST_DLL%" "%BUILD_DIR%\Qt6Testd.dll" >nul || exit /b 1
if not exist "%BUILD_DIR%\Qt6Networkd.dll" (
  echo Qt deployment did not produce Qt6Networkd.dll required by the ASR worker. 1>&2
  exit /b 1
)
if not exist "%BUILD_DIR%\Qt6Testd.dll" (
  echo Qt deployment did not produce Qt6Testd.dll required by the Debug tests. 1>&2
  exit /b 1
)
if not exist "%BUILD_DIR%\iconengines\qsvgicond.dll" (
  echo Qt deployment did not produce qsvgicond.dll required by the Windows tray icon. 1>&2
  exit /b 1
)
if not exist "%BUILD_DIR%\imageformats\qsvgd.dll" (
  echo Qt deployment did not produce qsvgd.dll required by the in-app logo. 1>&2
  exit /b 1
)
exit /b 0
