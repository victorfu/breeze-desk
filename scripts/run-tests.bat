@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
call scripts\setup-msvc.bat || exit /b 1
if not exist "build\debug\CMakeCache.txt" call scripts\build.bat || exit /b 1
for /f "tokens=2 delims==" %%A in ('findstr /b "Qt6_DIR:PATH=" "build\debug\CMakeCache.txt"') do set "QT6_CMAKE_DIR=%%A"
if not defined QT6_CMAKE_DIR (
  echo Unable to read Qt6_DIR from the Debug CMake cache. 1>&2
  exit /b 1
)
for %%I in ("%QT6_CMAKE_DIR%\..\..\..") do set "QT_BIN=%%~fI\bin"
for %%I in ("%QT_BIN%\..") do set "QT_ROOT=%%~fI"
if not exist "%QT_BIN%\Qt6Testd.dll" (
  echo The matching Qt Debug test runtime is missing from %QT_BIN%. 1>&2
  exit /b 1
)
set "PATH=%QT_BIN%;%PATH%"
set "QT_PLUGIN_PATH=%QT_ROOT%\plugins"
ctest --test-dir build\debug --output-on-failure %* || exit /b 1
