@echo off
setlocal
cd /d "%~dp0\.."
call scripts\setup-msvc.bat || exit /b 1
if not defined SCCACHE_DIR set SCCACHE_DIR=%CD%\.cache\sccache
if defined BREEZEDESK_WHISPER_CPP_SOURCE_DIR (
  cmake --preset release "-DBREEZEDESK_WHISPER_CPP_SOURCE_DIR=%BREEZEDESK_WHISPER_CPP_SOURCE_DIR%" || exit /b 1
) else (
  cmake --preset release || exit /b 1
)
cmake --build --preset release --parallel || exit /b 1
