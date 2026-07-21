@echo off
setlocal EnableExtensions
cd /d "%~dp0\..\.."
set "PROJECT_ROOT=%CD%"
call "%PROJECT_ROOT%\scripts\setup-msvc.bat" || exit /b 1
set "IDENTITY_DIR=%PROJECT_ROOT%\build\package-identity"
cmake "-DBREEZEDESK_SOURCE_DIR=%PROJECT_ROOT%" "-DBREEZEDESK_IDENTITY_OUTPUT_DIR=%IDENTITY_DIR%" -P "%PROJECT_ROOT%\cmake\ReadProjectIdentity.cmake" || exit /b 1
for /f "usebackq delims=" %%I in ("%IDENTITY_DIR%\product-name.txt") do set "DEFAULT_PRODUCT_NAME=%%I"
for /f "usebackq delims=" %%I in ("%IDENTITY_DIR%\release-executable-name.txt") do set "DEFAULT_RELEASE_EXECUTABLE_NAME=%%I"
for /f "usebackq delims=" %%I in ("%IDENTITY_DIR%\worker-executable-name.txt") do set "DEFAULT_WORKER_EXECUTABLE_NAME=%%I"
for /f "usebackq delims=" %%I in ("%IDENTITY_DIR%\cli-executable-name.txt") do set "DEFAULT_CLI_EXECUTABLE_NAME=%%I"
for /f "usebackq delims=" %%I in ("%IDENTITY_DIR%\windows-product-id.txt") do set "DEFAULT_WINDOWS_PRODUCT_ID=%%I"
if not defined BREEZEDESK_PRODUCT_NAME set "BREEZEDESK_PRODUCT_NAME=%DEFAULT_PRODUCT_NAME%"
if not defined BREEZEDESK_RELEASE_EXECUTABLE_NAME set "BREEZEDESK_RELEASE_EXECUTABLE_NAME=%DEFAULT_RELEASE_EXECUTABLE_NAME%"
if not defined BREEZEDESK_WORKER_EXECUTABLE_NAME set "BREEZEDESK_WORKER_EXECUTABLE_NAME=%DEFAULT_WORKER_EXECUTABLE_NAME%"
if not defined BREEZEDESK_CLI_EXECUTABLE_NAME set "BREEZEDESK_CLI_EXECUTABLE_NAME=%DEFAULT_CLI_EXECUTABLE_NAME%"
if not defined BREEZEDESK_WINDOWS_PRODUCT_ID set "BREEZEDESK_WINDOWS_PRODUCT_ID=%DEFAULT_WINDOWS_PRODUCT_ID%"
set "APP_EXE=%BREEZEDESK_RELEASE_EXECUTABLE_NAME%.exe"
set "WORKER_EXE=%BREEZEDESK_WORKER_EXECUTABLE_NAME%.exe"
set "CLI_EXE=%BREEZEDESK_CLI_EXECUTABLE_NAME%.exe"

set "PACKAGE_VARIANT=%~1"
if not defined PACKAGE_VARIANT set "PACKAGE_VARIANT=Universal"
if /I "%PACKAGE_VARIANT%"=="Universal" (
  set "PACKAGE_VARIANT=Universal"
  set "BACKEND=VULKAN"
  set "BACKEND_SLUG=vulkan"
  set "PREFERRED_BUILD=%PROJECT_ROOT%\build\windows-universal"
) else if /I "%PACKAGE_VARIANT%"=="CUDA" (
  set "PACKAGE_VARIANT=CUDA"
  set "BACKEND=CUDA"
  set "BACKEND_SLUG=cuda"
  set "PREFERRED_BUILD=%PROJECT_ROOT%\build\windows-cuda"
) else (
  echo Usage: packaging\windows\package.bat [Universal^|CUDA] [--msix] 1>&2
  exit /b 2
)
set "BUILD_MSIX=%BREEZEDESK_BUILD_MSIX%"
if /I "%~2"=="--msix" set "BUILD_MSIX=1"
if /I "%PACKAGE_VARIANT%"=="CUDA" if "%BUILD_MSIX%"=="1" (
  echo MSIX is emitted only by the Universal package to keep the required output name unambiguous. 1>&2
  exit /b 2
)

for %%T in (cmake.exe ninja.exe windeployqt.exe powershell.exe magick.exe dumpbin.exe) do (
  where %%T >nul 2>nul || (
    echo Required command %%T was not found on PATH. 1>&2
    exit /b 1
  )
)
if not defined BREEZEDESK_FFMPEG_DIR (
  echo BREEZEDESK_FFMPEG_DIR must point to an LGPL FFmpeg bin directory. 1>&2
  exit /b 1
)
if not exist "%BREEZEDESK_FFMPEG_DIR%\ffmpeg.exe" (
  echo Missing %BREEZEDESK_FFMPEG_DIR%\ffmpeg.exe. 1>&2
  exit /b 1
)
if not exist "%BREEZEDESK_FFMPEG_DIR%\ffprobe.exe" (
  echo Missing %BREEZEDESK_FFMPEG_DIR%\ffprobe.exe. 1>&2
  exit /b 1
)
if "%BREEZEDESK_PACKAGE_UPDATES%"=="1" if not defined BREEZEDESK_WINSPARKLE_DIR (
  echo BREEZEDESK_WINSPARKLE_DIR is required when updates are enabled. 1>&2
  exit /b 1
)
if defined BREEZEDESK_SIGNTOOL_CERT if defined BREEZEDESK_SIGNTOOL_SHA1 (
  echo Choose either BREEZEDESK_SIGNTOOL_CERT or BREEZEDESK_SIGNTOOL_SHA1, not both. 1>&2
  exit /b 1
)
call :verify_whisper_source || exit /b 1

if not exist "%PROJECT_ROOT%\build" (
  cmake -E make_directory "%PROJECT_ROOT%\build" || exit /b 1
)
set "VERSION_FILE=%PROJECT_ROOT%\build\package-windows-version.txt"
cmake "-DBREEZEDESK_SOURCE_DIR=%PROJECT_ROOT%" "-DBREEZEDESK_VERSION_OUTPUT=%VERSION_FILE%" -P "%PROJECT_ROOT%\cmake\ReadProjectVersion.cmake" || exit /b 1
set /p VERSION=<"%VERSION_FILE%"
if not defined VERSION (
  echo CMake project version is empty. 1>&2
  exit /b 1
)

set "FFMPEG_BUILDCONF=%PROJECT_ROOT%\build\ffmpeg-buildconf-windows.txt"
"%BREEZEDESK_FFMPEG_DIR%\ffmpeg.exe" -hide_banner -buildconf > "%FFMPEG_BUILDCONF%" 2>&1 || exit /b 1
findstr /C:"--enable-gpl" /C:"--enable-nonfree" "%FFMPEG_BUILDCONF%" >nul && (
  echo Packaging rejects FFmpeg builds with GPL or nonfree components enabled. 1>&2
  exit /b 1
)
for %%F in (--disable-gpl --disable-nonfree --disable-network --disable-autodetect --disable-shared --enable-static) do (
  findstr /C:"%%F" "%FFMPEG_BUILDCONF%" >nul || (
    echo Packaging requires FFmpeg build flag %%F. 1>&2
    exit /b 1
  )
)
set "FFMPEG_VERSION_OUTPUT=%PROJECT_ROOT%\build\ffmpeg-version-windows.txt"
"%BREEZEDESK_FFMPEG_DIR%\ffmpeg.exe" -hide_banner -version > "%FFMPEG_VERSION_OUTPUT%" 2>&1 || exit /b 1
findstr /B /C:"ffmpeg version 8.1.2 " "%FFMPEG_VERSION_OUTPUT%" >nul || (
  echo Packaging requires the pinned FFmpeg 8.1.2 release. 1>&2
  exit /b 1
)
for %%E in (ffmpeg.exe ffprobe.exe) do (
  dumpbin.exe /nologo /dependents "%BREEZEDESK_FFMPEG_DIR%\%%E" | findstr /I /C:"libwinpthread-1.dll" /C:"libgcc_s_" /C:"libstdc++-6.dll" >nul && (
    echo Packaging rejects %%E because its MinGW runtime is not statically linked. 1>&2
    exit /b 1
  )
)

set "ICON_PATH=%PROJECT_ROOT%\build\package-icon\breezedesk.ico"
cmake -E make_directory "%PROJECT_ROOT%\build\package-icon" || exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_ROOT%\packaging\windows\generate-icon.ps1" -OutputFile "%ICON_PATH%" || exit /b 1
if not exist "%ICON_PATH%" (
  echo ImageMagick did not create the Windows app icon. 1>&2
  exit /b 1
)

call :configure_build "%PREFERRED_BUILD%" "%BACKEND%" || exit /b 1
cmake --build "%PREFERRED_BUILD%" --parallel || exit /b 1
set "CPU_BUILD=%PROJECT_ROOT%\build\windows-cpu"
call :configure_build "%CPU_BUILD%" "CPU" || exit /b 1
cmake --build "%CPU_BUILD%" --target breezedesk-asr-worker --parallel || exit /b 1

set "STAGE_DIR=%PROJECT_ROOT%\build\package-windows-%PACKAGE_VARIANT%"
if exist "%STAGE_DIR%" cmake -E remove_directory "%STAGE_DIR%"
cmake -E make_directory "%STAGE_DIR%" "%PROJECT_ROOT%\dist" || exit /b 1
cmake --install "%PREFERRED_BUILD%" --prefix "%STAGE_DIR%" || exit /b 1
if not exist "%STAGE_DIR%\bin\%APP_EXE%" (
  echo CMake install did not produce %APP_EXE%. 1>&2
  exit /b 1
)
if not exist "%STAGE_DIR%\bin\%WORKER_EXE%" (
  echo The preferred native ASR worker is missing. 1>&2
  exit /b 1
)
if not exist "%CPU_BUILD%\%WORKER_EXE%" (
  echo The CPU fallback worker is missing. 1>&2
  exit /b 1
)
if not exist "%STAGE_DIR%\bin\%CLI_EXE%" (
  echo CMake install did not produce %CLI_EXE%. 1>&2
  exit /b 1
)
call :find_whisper_license || exit /b 1

cmake -E make_directory "%STAGE_DIR%\bin\workers" "%STAGE_DIR%\share\breezedesk\licenses" || exit /b 1
if defined BREEZEDESK_FFMPEG_LICENSE_DIR (
  set "FFMPEG_LICENSE_DIR=%BREEZEDESK_FFMPEG_LICENSE_DIR%"
) else (
  for %%I in ("%BREEZEDESK_FFMPEG_DIR%\..") do set "FFMPEG_LICENSE_DIR=%%~fI\LICENSES"
)
if not exist "%FFMPEG_LICENSE_DIR%\FFmpeg-LGPL-2.1.txt" (
  echo FFmpeg LGPL 2.1 text is missing. Set BREEZEDESK_FFMPEG_LICENSE_DIR or use build-ffmpeg-lgpl.ps1. 1>&2
  exit /b 1
)
if not exist "%FFMPEG_LICENSE_DIR%\FFmpeg-LGPL-3.0.txt" (
  echo FFmpeg LGPL 3.0 text is missing. Set BREEZEDESK_FFMPEG_LICENSE_DIR or use build-ffmpeg-lgpl.ps1. 1>&2
  exit /b 1
)
cmake -E copy_if_different "%STAGE_DIR%\bin\%WORKER_EXE%" "%STAGE_DIR%\bin\workers\%BREEZEDESK_WORKER_EXECUTABLE_NAME%-%BACKEND_SLUG%.exe" || exit /b 1
cmake -E copy_if_different "%CPU_BUILD%\%WORKER_EXE%" "%STAGE_DIR%\bin\workers\%BREEZEDESK_WORKER_EXECUTABLE_NAME%-cpu.exe" || exit /b 1
cmake -E copy_if_different "%BREEZEDESK_FFMPEG_DIR%\ffmpeg.exe" "%STAGE_DIR%\bin\ffmpeg.exe" || exit /b 1
cmake -E copy_if_different "%BREEZEDESK_FFMPEG_DIR%\ffprobe.exe" "%STAGE_DIR%\bin\ffprobe.exe" || exit /b 1
cmake -E copy_if_different "%PROJECT_ROOT%\LICENSE" "%STAGE_DIR%\share\breezedesk\licenses\%BREEZEDESK_PRODUCT_NAME%-MIT.txt" || exit /b 1
cmake -E copy_if_different "%PROJECT_ROOT%\THIRD_PARTY_NOTICES.md" "%STAGE_DIR%\share\breezedesk\licenses\THIRD_PARTY_NOTICES.md" || exit /b 1
cmake -E copy_if_different "%PROJECT_ROOT%\docs\licenses\Qt-LGPL-NOTICE.md" "%STAGE_DIR%\share\breezedesk\licenses\Qt-LGPL-NOTICE.md" || exit /b 1
cmake -E copy_if_different "%PROJECT_ROOT%\docs\licenses\LGPL-3.0-only.txt" "%STAGE_DIR%\share\breezedesk\licenses\LGPL-3.0-only.txt" || exit /b 1
cmake -E copy_if_different "%PROJECT_ROOT%\docs\licenses\GPL-3.0-only.txt" "%STAGE_DIR%\share\breezedesk\licenses\GPL-3.0-only.txt" || exit /b 1
cmake -E copy_if_different "%PREFERRED_BUILD%\generated\Qt-SOURCE.txt" "%STAGE_DIR%\share\breezedesk\licenses\Qt-SOURCE.txt" || exit /b 1
cmake -E copy_if_different "%WHISPER_LICENSE%" "%STAGE_DIR%\share\breezedesk\licenses\whisper.cpp-MIT.txt" || exit /b 1
cmake -E copy_if_different "%PROJECT_ROOT%\resources\icons\lucide\LICENSE" "%STAGE_DIR%\share\breezedesk\licenses\Lucide-LICENSE.txt" || exit /b 1
cmake -E copy_if_different "%PROJECT_ROOT%\resources\icons\lucide\SOURCE.md" "%STAGE_DIR%\share\breezedesk\licenses\Lucide-SOURCE.md" || exit /b 1
cmake -E copy_if_different "%FFMPEG_BUILDCONF%" "%STAGE_DIR%\share\breezedesk\licenses\FFmpeg-BUILD_CONFIGURATION.txt" || exit /b 1
cmake -E copy_if_different "%FFMPEG_LICENSE_DIR%\FFmpeg-LGPL-2.1.txt" "%STAGE_DIR%\share\breezedesk\licenses\FFmpeg-LGPL-2.1.txt" || exit /b 1
cmake -E copy_if_different "%FFMPEG_LICENSE_DIR%\FFmpeg-LGPL-3.0.txt" "%STAGE_DIR%\share\breezedesk\licenses\FFmpeg-LGPL-3.0.txt" || exit /b 1
(
  echo FFmpeg 8.1.2
  echo Source archive: https://ffmpeg.org/releases/ffmpeg-8.1.2.tar.xz
  echo Source SHA-256: 464beb5e7bf0c311e68b45ae2f04e9cc2af88851abb4082231742a74d97b524c
) > "%STAGE_DIR%\share\breezedesk\licenses\FFmpeg-SOURCE.txt"

if defined BREEZEDESK_WINSPARKLE_DIR (
  if not "%BREEZEDESK_PACKAGE_UPDATES%"=="1" (
    echo BREEZEDESK_WINSPARKLE_DIR requires BREEZEDESK_PACKAGE_UPDATES=1. 1>&2
    exit /b 1
  )
  if not exist "%BREEZEDESK_WINSPARKLE_DIR%\WinSparkle.dll" (
    echo BREEZEDESK_WINSPARKLE_DIR does not contain WinSparkle.dll. 1>&2
    exit /b 1
  )
  cmake -E copy_if_different "%BREEZEDESK_WINSPARKLE_DIR%\WinSparkle.dll" "%STAGE_DIR%\bin\WinSparkle.dll" || exit /b 1
  if exist "%BREEZEDESK_WINSPARKLE_DIR%\COPYING" (
    cmake -E copy_if_different "%BREEZEDESK_WINSPARKLE_DIR%\COPYING" "%STAGE_DIR%\share\breezedesk\licenses\WinSparkle-LICENSE.txt" || exit /b 1
  )
  if exist "%BREEZEDESK_WINSPARKLE_DIR%\COPYING.expat" (
    cmake -E copy_if_different "%BREEZEDESK_WINSPARKLE_DIR%\COPYING.expat" "%STAGE_DIR%\share\breezedesk\licenses\WinSparkle-Expat-LICENSE.txt" || exit /b 1
  )
  if exist "%BREEZEDESK_WINSPARKLE_DIR%\SOURCE.txt" (
    cmake -E copy_if_different "%BREEZEDESK_WINSPARKLE_DIR%\SOURCE.txt" "%STAGE_DIR%\share\breezedesk\licenses\WinSparkle-SOURCE.txt" || exit /b 1
  )
)
if /I "%PACKAGE_VARIANT%"=="CUDA" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_ROOT%\packaging\windows\deploy-cuda-runtime.ps1" -Worker "%STAGE_DIR%\bin\workers\%BREEZEDESK_WORKER_EXECUTABLE_NAME%-cuda.exe" -Destination "%STAGE_DIR%\bin" -LicenseDirectory "%STAGE_DIR%\share\breezedesk\licenses" || exit /b 1
)

windeployqt --release --force --compiler-runtime --qmldir "%PROJECT_ROOT%\src\qml" --translations en,zh_TW "%STAGE_DIR%\bin\%APP_EXE%" "%STAGE_DIR%\bin\%CLI_EXE%" "%STAGE_DIR%\bin\%WORKER_EXE%" "%STAGE_DIR%\bin\workers\%BREEZEDESK_WORKER_EXECUTABLE_NAME%-%BACKEND_SLUG%.exe" "%STAGE_DIR%\bin\workers\%BREEZEDESK_WORKER_EXECUTABLE_NAME%-cpu.exe" || exit /b 1
if not exist "%STAGE_DIR%\bin\Qt6Network.dll" (
  echo Qt deployment did not produce Qt6Network.dll required by the ASR workers. 1>&2
  exit /b 1
)
if not exist "%STAGE_DIR%\bin\iconengines\qsvgicon.dll" (
  echo Qt deployment did not produce the SVG icon engine required by SVG-based UI icons. 1>&2
  exit /b 1
)
if not exist "%STAGE_DIR%\bin\imageformats\qsvg.dll" (
  echo Qt deployment did not produce the SVG image plugin required by SVG-based UI icons. 1>&2
  exit /b 1
)
call :sign_if_requested "%STAGE_DIR%" || exit /b 1

set "MAKENSIS=%ProgramFiles(x86)%\NSIS\makensis.exe"
if not exist "%MAKENSIS%" for /f "delims=" %%I in ('where makensis.exe 2^>nul') do set "MAKENSIS=%%I"
if not exist "%MAKENSIS%" (
  echo NSIS makensis.exe is required to create the installer. 1>&2
  exit /b 1
)
set "INSTALLER=%PROJECT_ROOT%\dist\%BREEZEDESK_PRODUCT_NAME%-%VERSION%-Windows-x64-%PACKAGE_VARIANT%-Setup.exe"
"%MAKENSIS%" "/DPROJECT_ROOT=%PROJECT_ROOT%" "/DVERSION=%VERSION%" "/DVARIANT=%PACKAGE_VARIANT%" "/DSTAGE_DIR=%STAGE_DIR%" "/DOUTPUT_FILE=%INSTALLER%" "/DICON_FILE=%ICON_PATH%" "/DPRODUCT_NAME=%BREEZEDESK_PRODUCT_NAME%" "/DEXECUTABLE_NAME=%BREEZEDESK_RELEASE_EXECUTABLE_NAME%" "/DPRODUCT_ID=%BREEZEDESK_WINDOWS_PRODUCT_ID%" "%PROJECT_ROOT%\packaging\windows\installer.nsi" || exit /b 1
call :sign_if_requested "%INSTALLER%" || exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_ROOT%\packaging\windows\write-checksum.ps1" "%INSTALLER%" || exit /b 1

if "%BUILD_MSIX%"=="1" (
  set "MSIX=%PROJECT_ROOT%\dist\%BREEZEDESK_PRODUCT_NAME%-%VERSION%-Windows-x64.msix"
  powershell -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_ROOT%\packaging\windows\create-msix.ps1" -StageDirectory "%STAGE_DIR%" -OutputFile "%MSIX%" -Version "%VERSION%" -ProductName "%BREEZEDESK_PRODUCT_NAME%" -ExecutableName "%BREEZEDESK_RELEASE_EXECUTABLE_NAME%" -ProductId "%BREEZEDESK_WINDOWS_PRODUCT_ID%" || exit /b 1
  call :sign_if_requested "%MSIX%" || exit /b 1
  powershell -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_ROOT%\packaging\windows\write-checksum.ps1" "%MSIX%" || exit /b 1
)

echo %INSTALLER%
exit /b 0

:sign_if_requested
if not defined BREEZEDESK_SIGNTOOL_CERT if not defined BREEZEDESK_SIGNTOOL_SHA1 exit /b 0
powershell -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_ROOT%\packaging\windows\sign-artifacts.ps1" "%~1"
exit /b %ERRORLEVEL%

:find_whisper_license
set "WHISPER_LICENSE=%PREFERRED_BUILD%\_deps\whisper_cpp-src\LICENSE"
if defined BREEZEDESK_WHISPER_CPP_SOURCE_DIR set "WHISPER_LICENSE=%BREEZEDESK_WHISPER_CPP_SOURCE_DIR%\LICENSE"
if exist "%WHISPER_LICENSE%" exit /b 0
set "WHISPER_LICENSE=%PREFERRED_BUILD%\_deps\whisper.cpp-src\LICENSE"
if exist "%WHISPER_LICENSE%" exit /b 0
echo The pinned whisper.cpp LICENSE file could not be located. 1>&2
exit /b 1

:verify_whisper_source
if not defined BREEZEDESK_WHISPER_CPP_SOURCE_DIR exit /b 0
where git.exe >nul 2>nul || (
  echo git.exe is required to verify BREEZEDESK_WHISPER_CPP_SOURCE_DIR. 1>&2
  exit /b 1
)
set "WHISPER_EXPECTED_REF=f049fff95a089aa9969deb009cdd4892b3e74916"
set "WHISPER_ACTUAL_REF="
for /f "delims=" %%I in ('git -C "%BREEZEDESK_WHISPER_CPP_SOURCE_DIR%" rev-parse HEAD 2^>nul') do set "WHISPER_ACTUAL_REF=%%I"
if /I not "%WHISPER_ACTUAL_REF%"=="%WHISPER_EXPECTED_REF%" (
  echo BREEZEDESK_WHISPER_CPP_SOURCE_DIR is %WHISPER_ACTUAL_REF%, expected %WHISPER_EXPECTED_REF%. 1>&2
  exit /b 1
)
exit /b 0

:configure_build
set "CONFIGURE_DIR=%~1"
set "CONFIGURE_BACKEND=%~2"
if "%BREEZEDESK_PACKAGE_UPDATES%"=="1" (
  if not defined BREEZEDESK_APPCAST_URL (
    echo BREEZEDESK_APPCAST_URL is required when updates are enabled. 1>&2
    exit /b 1
  )
  if not defined BREEZEDESK_EDDSA_PUBLIC_KEY (
    echo BREEZEDESK_EDDSA_PUBLIC_KEY is required when updates are enabled. 1>&2
    exit /b 1
  )
  cmake -S "%PROJECT_ROOT%" -B "%CONFIGURE_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBREEZEDESK_BUILD_TESTS=OFF -DBREEZEDESK_ENABLE_WHISPER=ON "-DBREEZEDESK_PRODUCT_NAME=%BREEZEDESK_PRODUCT_NAME%" "-DBREEZEDESK_RELEASE_EXECUTABLE_NAME=%BREEZEDESK_RELEASE_EXECUTABLE_NAME%" "-DBREEZEDESK_WORKER_EXECUTABLE_NAME=%BREEZEDESK_WORKER_EXECUTABLE_NAME%" "-DBREEZEDESK_CLI_EXECUTABLE_NAME=%BREEZEDESK_CLI_EXECUTABLE_NAME%" "-DBREEZEDESK_WINDOWS_PRODUCT_ID=%BREEZEDESK_WINDOWS_PRODUCT_ID%" "-DBREEZEDESK_WINDOWS_BACKEND=%CONFIGURE_BACKEND%" "-DBREEZEDESK_WINDOWS_ICON_PATH=%ICON_PATH%" -DBREEZEDESK_ENABLE_UPDATES=ON "-DBREEZEDESK_APPCAST_URL=%BREEZEDESK_APPCAST_URL%" "-DBREEZEDESK_EDDSA_PUBLIC_KEY=%BREEZEDESK_EDDSA_PUBLIC_KEY%" "-DBREEZEDESK_WHISPER_CPP_SOURCE_DIR=%BREEZEDESK_WHISPER_CPP_SOURCE_DIR%"
) else (
  cmake -S "%PROJECT_ROOT%" -B "%CONFIGURE_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBREEZEDESK_BUILD_TESTS=OFF -DBREEZEDESK_ENABLE_WHISPER=ON "-DBREEZEDESK_PRODUCT_NAME=%BREEZEDESK_PRODUCT_NAME%" "-DBREEZEDESK_RELEASE_EXECUTABLE_NAME=%BREEZEDESK_RELEASE_EXECUTABLE_NAME%" "-DBREEZEDESK_WORKER_EXECUTABLE_NAME=%BREEZEDESK_WORKER_EXECUTABLE_NAME%" "-DBREEZEDESK_CLI_EXECUTABLE_NAME=%BREEZEDESK_CLI_EXECUTABLE_NAME%" "-DBREEZEDESK_WINDOWS_PRODUCT_ID=%BREEZEDESK_WINDOWS_PRODUCT_ID%" "-DBREEZEDESK_WINDOWS_BACKEND=%CONFIGURE_BACKEND%" "-DBREEZEDESK_WINDOWS_ICON_PATH=%ICON_PATH%" -DBREEZEDESK_ENABLE_UPDATES=OFF "-DBREEZEDESK_WHISPER_CPP_SOURCE_DIR=%BREEZEDESK_WHISPER_CPP_SOURCE_DIR%"
)
exit /b %ERRORLEVEL%
