@echo off
if /i "%VSCMD_ARG_HOST_ARCH%"=="x64" if /i "%VSCMD_ARG_TGT_ARCH%"=="x64" if defined INCLUDE (
  where cl.exe >nul 2>nul && exit /b 0
)

set "BREEZEDESK_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%BREEZEDESK_VSWHERE%" (
  echo Visual Studio Installer's vswhere.exe was not found. Install Visual Studio 2022 Build Tools with the Desktop development with C++ workload. 1>&2
  exit /b 1
)
set "BREEZEDESK_VS_INSTALL="
set "BREEZEDESK_VSWHERE_OUTPUT=%TEMP%\breezedesk-vswhere-%RANDOM%-%RANDOM%.txt"
"%BREEZEDESK_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%BREEZEDESK_VSWHERE_OUTPUT%"
if errorlevel 1 (
  del /q "%BREEZEDESK_VSWHERE_OUTPUT%" >nul 2>nul
  echo Visual Studio Installer failed while locating the C++ build tools. 1>&2
  exit /b 1
)
set /p "BREEZEDESK_VS_INSTALL="<"%BREEZEDESK_VSWHERE_OUTPUT%"
del /q "%BREEZEDESK_VSWHERE_OUTPUT%" >nul 2>nul
if not defined BREEZEDESK_VS_INSTALL (
  echo Visual Studio 2022 C++ build tools were not found. Install the Desktop development with C++ workload. 1>&2
  exit /b 1
)
if not exist "%BREEZEDESK_VS_INSTALL%\Common7\Tools\VsDevCmd.bat" (
  echo VsDevCmd.bat is missing from the selected Visual Studio installation. 1>&2
  exit /b 1
)
call "%BREEZEDESK_VS_INSTALL%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 -no_logo || exit /b 1
if not defined INCLUDE (
  echo Visual Studio initialized without the C++ or Windows SDK include paths. 1>&2
  exit /b 1
)
if /i not "%VSCMD_ARG_HOST_ARCH%"=="x64" (
  echo Visual Studio initialized with a non-x64 host toolchain. 1>&2
  exit /b 1
)
if /i not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
  echo Visual Studio initialized with a non-x64 target toolchain. 1>&2
  exit /b 1
)
where cl.exe >nul 2>nul || (
  echo Visual Studio initialized without an accessible C++ compiler. 1>&2
  exit /b 1
)
exit /b 0
