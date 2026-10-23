

Unicode true

!define APP_NAME "STL Preview"
!ifndef APP_VERSION
  !define APP_VERSION "1.0.0"
!endif
!define APP_PUBLISHER "STLPreview"
!define APP_KEY "Software\STLPreview"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\STLPreview"
!define CLASSES_KEY "Software\Classes"

!define CLSID_THUMBNAIL "{2E4A0F31-7C5B-4D81-9E62-1A0B3C4D5E6F}"
!define CLSID_PREVIEW "{2E4A0F32-7C5B-4D81-9E62-1A0B3C4D5E6F}"
!define APPID_PREVIEW "{2E4A0F33-7C5B-4D81-9E62-1A0B3C4D5E6F}"
!define SHELLEX_THUMBNAIL "{e357fccd-a995-4576-b01f-234630154e96}"
!define SHELLEX_PREVIEW "{8895b1c6-b41f-4c1c-a562-0d564250836f}"

!ifndef SOURCE_DIR
  !define SOURCE_DIR "..\build\bin\Release"
!endif
!ifndef OUT_DIR
  !define OUT_DIR "..\dist"
!endif

Name "${APP_NAME}"
OutFile "${OUT_DIR}\STLPreview-Setup-${APP_VERSION}.exe"
InstallDir "$LOCALAPPDATA\Programs\STLPreview\${APP_VERSION}"
RequestExecutionLevel user
SetCompressor /SOLID lzma
BrandingText "${APP_NAME} ${APP_VERSION}"

!include "MUI2.nsh"
!include "FileFunc.nsh"

VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey /LANG=1049 "ProductName" "${APP_NAME}"
VIAddVersionKey /LANG=1049 "FileDescription" "Установщик эскизов и предпросмотра STL для проводника Windows"
VIAddVersionKey /LANG=1049 "FileVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1049 "ProductVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1049 "CompanyName" "${APP_PUBLISHER}"
VIAddVersionKey /LANG=1049 "LegalCopyright" ""

!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Установка ${APP_NAME}"
!define MUI_WELCOMEPAGE_TEXT "Эскизы и 3D-предпросмотр STL-файлов в проводнике Windows.$\r$\n$\r$\nУстановка идёт в профиль пользователя, права администратора не нужны."
!define MUI_FINISHPAGE_TITLE "Готово"
!define MUI_FINISHPAGE_TEXT "Эскизы и область предпросмотра включены для файлов .stl.$\r$\n$\r$\nВыберите STL-файл в проводнике и нажмите Alt+P, чтобы открыть область предпросмотра.$\r$\n$\r$\nПроводник кэширует эскизы: если у старого файла осталась прежняя иконка, проверьте на новом файле."
!define MUI_FINISHPAGE_NOAUTOCLOSE

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "Russian"

Var OLD_INSTALL_DIR

!macro RegisterShellExtension DLL_PATH HOST_PATH
  WriteRegStr HKCU "${CLASSES_KEY}\CLSID\${CLSID_THUMBNAIL}" "" "STL Thumbnail Provider"
  WriteRegStr HKCU "${CLASSES_KEY}\CLSID\${CLSID_THUMBNAIL}\InprocServer32" "" "${DLL_PATH}"
  WriteRegStr HKCU "${CLASSES_KEY}\CLSID\${CLSID_THUMBNAIL}\InprocServer32" "ThreadingModel" "Apartment"

  WriteRegStr HKCU "${CLASSES_KEY}\CLSID\${CLSID_PREVIEW}" "" "STL Preview Handler"
  WriteRegStr HKCU "${CLASSES_KEY}\CLSID\${CLSID_PREVIEW}\InprocServer32" "" "${DLL_PATH}"
  WriteRegStr HKCU "${CLASSES_KEY}\CLSID\${CLSID_PREVIEW}\InprocServer32" "ThreadingModel" "Apartment"
  WriteRegStr HKCU "${CLASSES_KEY}\CLSID\${CLSID_PREVIEW}\LocalServer32" "" '"${HOST_PATH}"'

  DeleteRegKey HKCU "${CLASSES_KEY}\AppID\${APPID_PREVIEW}"

  WriteRegStr HKCU "${CLASSES_KEY}\.stl\shellex\${SHELLEX_THUMBNAIL}" "" "${CLSID_THUMBNAIL}"
  WriteRegStr HKCU "${CLASSES_KEY}\.stl\shellex\${SHELLEX_PREVIEW}" "" "${CLSID_PREVIEW}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\PreviewHandlers" "${CLSID_PREVIEW}" "STL Preview Handler"

  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
!macroend

!macro UnregisterShellExtension
  DeleteRegKey HKCU "${CLASSES_KEY}\CLSID\${CLSID_THUMBNAIL}"
  DeleteRegKey HKCU "${CLASSES_KEY}\CLSID\${CLSID_PREVIEW}"
  DeleteRegKey HKCU "${CLASSES_KEY}\AppID\${APPID_PREVIEW}"
  DeleteRegKey HKCU "${CLASSES_KEY}\.stl\shellex\${SHELLEX_THUMBNAIL}"
  DeleteRegKey HKCU "${CLASSES_KEY}\.stl\shellex\${SHELLEX_PREVIEW}"
  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\PreviewHandlers" "${CLSID_PREVIEW}"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
!macroend

!macro StopPreviewHost
  nsExec::Exec 'taskkill /F /IM STLPreviewHost.exe'
  Pop $0
!macroend

Function .onInit

  ReadRegStr $OLD_INSTALL_DIR HKCU "${APP_KEY}" "InstallDir"
FunctionEnd

Section "Установка" SecInstall
  SectionIn RO

  SetRegView 64
  SetShellVarContext current
  SetOutPath "$INSTDIR"

  DetailPrint "Остановка хоста предпросмотра"
  !insertmacro StopPreviewHost

  ${If} $OLD_INSTALL_DIR != ""
  ${AndIf} $OLD_INSTALL_DIR != "$INSTDIR"
    !insertmacro UnregisterShellExtension
  ${EndIf}

  DetailPrint "Копирование файлов"
  File "${SOURCE_DIR}\STLPreview.dll"
  File "${SOURCE_DIR}\STLPreviewHost.exe"

  DetailPrint "Регистрация расширения"
  !insertmacro RegisterShellExtension "$INSTDIR\STLPreview.dll" "$INSTDIR\STLPreviewHost.exe"

  WriteRegStr HKCU "${APP_KEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${APP_KEY}" "Version" "${APP_VERSION}"

  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "STL Preview для проводника"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "${APP_PUBLISHER}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\STLPreview.dll"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1

  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" $0

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ${If} $OLD_INSTALL_DIR != ""
  ${AndIf} $OLD_INSTALL_DIR != "$INSTDIR"
    RMDir /r "$OLD_INSTALL_DIR"
    ClearErrors
  ${EndIf}
SectionEnd

Section "Uninstall"
  SetRegView 64
  SetShellVarContext current

  !insertmacro StopPreviewHost
  !insertmacro UnregisterShellExtension

  Delete "$INSTDIR\STLPreview.dll"
  Delete "$INSTDIR\STLPreviewHost.exe"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"

  RMDir /r "$LOCALAPPDATA\Programs\STLPreview"

  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  DeleteRegKey HKCU "${APP_KEY}"
SectionEnd
