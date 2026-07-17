@echo off
setlocal
cd /d "%~dp0\.."
lrelease translations\breezedesk_en.ts translations\breezedesk_zh_TW.ts || exit /b 1
