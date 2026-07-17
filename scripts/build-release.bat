@echo off
setlocal
cd /d "%~dp0\.."
if not defined SCCACHE_DIR set SCCACHE_DIR=%CD%\.cache\sccache
set EXTRA_ARGS=
if defined BREEZEDESK_WHISPER_CPP_SOURCE_DIR set EXTRA_ARGS=-DBREEZEDESK_WHISPER_CPP_SOURCE_DIR=%BREEZEDESK_WHISPER_CPP_SOURCE_DIR%
cmake --preset release %EXTRA_ARGS% || exit /b 1
cmake --build --preset release --parallel || exit /b 1
