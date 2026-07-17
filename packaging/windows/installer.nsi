Unicode true

!include "MUI2.nsh"
!include "FileFunc.nsh"

!ifndef VERSION
  !error "VERSION must be supplied by package.bat"
!endif
!ifndef VARIANT
  !error "VARIANT must be supplied by package.bat"
!endif
!ifndef STAGE_DIR
  !error "STAGE_DIR must be supplied by package.bat"
!endif
!ifndef OUTPUT_FILE
  !error "OUTPUT_FILE must be supplied by package.bat"
!endif
!ifndef PRODUCT_NAME
  !error "PRODUCT_NAME must be supplied by package.bat"
!endif
!ifndef EXECUTABLE_NAME
  !error "EXECUTABLE_NAME must be supplied by package.bat"
!endif
!ifndef PRODUCT_ID
  !error "PRODUCT_ID must be supplied by package.bat"
!endif

Name "${PRODUCT_NAME}"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\${PRODUCT_NAME}"
InstallDirRegKey HKCU "Software\${PRODUCT_ID}" "InstallLocation"
RequestExecutionLevel user
SetCompressor /SOLID lzma
BrandingText "${PRODUCT_NAME} ${VERSION} ${VARIANT}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=1033 "ProductName" "${PRODUCT_NAME}"
VIAddVersionKey /LANG=1033 "FileDescription" "${PRODUCT_NAME} ${VARIANT} installer"
VIAddVersionKey /LANG=1033 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=1033 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "MIT License"

!define MUI_ABORTWARNING
!ifdef ICON_FILE
  !define MUI_ICON "${ICON_FILE}"
  !define MUI_UNICON "${ICON_FILE}"
!endif
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${PROJECT_ROOT}\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "TradChinese"

Function RegisterMediaExtension
  Exch $0
  WriteRegStr HKCU "Software\Classes\$0\OpenWithProgids" "${PRODUCT_ID}.Media" ""
  Pop $0
FunctionEnd

Section "${PRODUCT_NAME}" SEC_APP
  SetShellVarContext current
  SetOutPath "$INSTDIR"
  File /r "${STAGE_DIR}\*"

  WriteRegStr HKCU "Software\${PRODUCT_ID}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\${PRODUCT_ID}" "Variant" "${VARIANT}"
  WriteRegStr HKCU "Software\Classes\${PRODUCT_ID}.Media" "" "${PRODUCT_NAME} media"
  WriteRegStr HKCU "Software\Classes\${PRODUCT_ID}.Media\DefaultIcon" "" "$INSTDIR\bin\${EXECUTABLE_NAME}.exe,0"
  WriteRegStr HKCU "Software\Classes\${PRODUCT_ID}.Media\shell\open\command" "" '$"$INSTDIR\bin\${EXECUTABLE_NAME}.exe$" $"%1$"'
  Push ".wav"
  Call RegisterMediaExtension
  Push ".mp3"
  Call RegisterMediaExtension
  Push ".m4a"
  Call RegisterMediaExtension
  Push ".aac"
  Call RegisterMediaExtension
  Push ".flac"
  Call RegisterMediaExtension
  Push ".ogg"
  Call RegisterMediaExtension
  Push ".opus"
  Call RegisterMediaExtension
  Push ".mp4"
  Call RegisterMediaExtension
  Push ".mov"
  Call RegisterMediaExtension
  Push ".mkv"
  Call RegisterMediaExtension
  Push ".webm"
  Call RegisterMediaExtension

  CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
  CreateShortcut "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" "$INSTDIR\bin\${EXECUTABLE_NAME}.exe"
  CreateShortcut "$DESKTOP\${PRODUCT_NAME}.lnk" "$INSTDIR\bin\${EXECUTABLE_NAME}.exe"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_ID}" "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_ID}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_ID}" "DisplayIcon" "$INSTDIR\bin\${EXECUTABLE_NAME}.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_ID}" "UninstallString" '$"$INSTDIR\Uninstall.exe$"'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_ID}" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_ID}" "NoRepair" 1

  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd

Section "Uninstall"
  SetShellVarContext current
  Delete "$DESKTOP\${PRODUCT_NAME}.lnk"
  RMDir /r "$SMPROGRAMS\${PRODUCT_NAME}"
  DeleteRegKey HKCU "Software\Classes\${PRODUCT_ID}.Media"
  DeleteRegValue HKCU "Software\Classes\.wav\OpenWithProgids" "${PRODUCT_ID}.Media"
  DeleteRegValue HKCU "Software\Classes\.mp3\OpenWithProgids" "${PRODUCT_ID}.Media"
  DeleteRegValue HKCU "Software\Classes\.m4a\OpenWithProgids" "${PRODUCT_ID}.Media"
  DeleteRegValue HKCU "Software\Classes\.aac\OpenWithProgids" "${PRODUCT_ID}.Media"
  DeleteRegValue HKCU "Software\Classes\.flac\OpenWithProgids" "${PRODUCT_ID}.Media"
  DeleteRegValue HKCU "Software\Classes\.ogg\OpenWithProgids" "${PRODUCT_ID}.Media"
  DeleteRegValue HKCU "Software\Classes\.opus\OpenWithProgids" "${PRODUCT_ID}.Media"
  DeleteRegValue HKCU "Software\Classes\.mp4\OpenWithProgids" "${PRODUCT_ID}.Media"
  DeleteRegValue HKCU "Software\Classes\.mov\OpenWithProgids" "${PRODUCT_ID}.Media"
  DeleteRegValue HKCU "Software\Classes\.mkv\OpenWithProgids" "${PRODUCT_ID}.Media"
  DeleteRegValue HKCU "Software\Classes\.webm\OpenWithProgids" "${PRODUCT_ID}.Media"
  DeleteRegKey HKCU "Software\${PRODUCT_ID}"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_ID}"
  RMDir /r "$INSTDIR"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd
