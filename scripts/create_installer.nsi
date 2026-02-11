; NSIS Installer Script for Metasiberia Beta
; Creates installer with all required files

; Version can be overridden via command line: /DMyAppVersion="0.0.2"
!ifndef MyAppVersion
  !define MyAppVersion "0.0.2"
!endif

!define MyAppName "Metasiberia Beta"
!define MyAppPublisher "Metasiberia"
!define MyAppAuthor "Denis Shipilov"
!define MyAppURL "https://substrata.info/"
!define MyAppExeName "gui_client.exe"

; Source and output directories can be overridden via command line
!ifndef SourceDir
  !define SourceDir "C:\programming\substrata_output_qt\vs2022\cyberspace_x64\Release"
!endif
!ifndef OutputDir
  !define OutputDir "C:\programming\substrata_output_qt\installers"
!endif

; Include Modern UI
!include "MUI2.nsh"
!include "LogicLib.nsh"

; Installer Attributes
Name "${MyAppName}"
OutFile "${OutputDir}\MetasiberiaBeta-Setup-v${MyAppVersion}.exe"
InstallDir "$PROGRAMFILES64\${MyAppName}"
RequestExecutionLevel admin
Unicode True

; Compression - use non-solid LZMA to avoid integrity check failures with large installers
SetCompressor lzma
SetCompressorDictSize 32

; Interface Settings
!define MUI_ABORTWARNING
!define MUI_ICON "${SourceDir}\data\resources\icons\metasiberia.ico"
!define MUI_UNICON "${SourceDir}\data\resources\icons\metasiberia.ico"
!define MUI_LANGDLL_ALLLANGUAGES
!define MUI_LANGDLL_WINDOWTITLE "Installer Language"
!define MUI_LANGDLL_INFO "Please choose the installer language:"

; Pages
!insertmacro MUI_PAGE_WELCOME
!ifdef LICENSE_FILE
  !insertmacro MUI_PAGE_LICENSE "${LICENSE_FILE}"
!endif
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\${MyAppExeName}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

; Languages
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Russian"

Function .onInit
  ; Show language chooser with English preselected by default.
  StrCpy $LANGUAGE ${LANG_ENGLISH}
  !insertmacro MUI_LANGDLL_DISPLAY

  ; If an older installation exists, offer to uninstall it first.
  ReadRegStr $R1 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${MyAppName}" "InstallLocation"
  ${If} $R1 != ""
    StrCpy $INSTDIR $R1
  ${EndIf}

  ReadRegStr $R0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${MyAppName}" "UninstallString"
  ${If} $R0 != ""
    MessageBox MB_ICONQUESTION|MB_YESNO "A previous ${MyAppName} installation was found. Uninstall it now?" IDNO done
    ExecWait '"$R0" /S'
  ${EndIf}
done:
FunctionEnd

Function .onInstSuccess
  !insertmacro MUI_LANGDLL_SAVELANGUAGE
FunctionEnd

; Installer Sections
Section "Main Application" SecMain
  SectionIn RO
  
  SetOutPath "$INSTDIR"
  
  ; Main executables
  File "${SourceDir}\${MyAppExeName}"
  !if /FileExists "${SourceDir}\server.exe"
    File "${SourceDir}\server.exe"
  !endif
  !if /FileExists "${SourceDir}\browser_process.exe"
    File "${SourceDir}\browser_process.exe"
  !endif
  
  ; Qt DLLs
  File /nonfatal "${SourceDir}\Qt5Core.dll"
  File /nonfatal "${SourceDir}\Qt5Gui.dll"
  File /nonfatal "${SourceDir}\Qt5Widgets.dll"
  File /nonfatal "${SourceDir}\Qt5OpenGL.dll"
  File /nonfatal "${SourceDir}\Qt5Multimedia.dll"
  File /nonfatal "${SourceDir}\Qt5MultimediaWidgets.dll"
  File /nonfatal "${SourceDir}\Qt5Network.dll"
  File /nonfatal "${SourceDir}\Qt5Gamepad.dll"
  
  ; CEF files
  File /nonfatal "${SourceDir}\libcef.dll"
  File /nonfatal "${SourceDir}\chrome_elf.dll"
  File /nonfatal "${SourceDir}\icudtl.dat"
  File /nonfatal "${SourceDir}\resources.pak"
  File /nonfatal "${SourceDir}\v8_context_snapshot.bin"
  File /nonfatal "${SourceDir}\chrome_100_percent.pak"
  File /nonfatal "${SourceDir}\chrome_200_percent.pak"
  File /nonfatal "${SourceDir}\d3dcompiler_47.dll"
  File /nonfatal "${SourceDir}\libEGL.dll"
  File /nonfatal "${SourceDir}\libGLESv2.dll"
  File /nonfatal "${SourceDir}\vulkan-1.dll"
  File /nonfatal "${SourceDir}\vk_swiftshader.dll"
  File /nonfatal "${SourceDir}\vk_swiftshader_icd.json"
  
  ; SDL
  File /nonfatal "${SourceDir}\SDL2.dll"
  
  ; DirectX
  File /nonfatal "${SourceDir}\dxcompiler.dll"
  File /nonfatal "${SourceDir}\dxil.dll"
  
  ; Qt plugins directories
  SetOutPath "$INSTDIR\platforms"
  File /nonfatal /r "${SourceDir}\platforms\*"
  
  SetOutPath "$INSTDIR\styles"
  File /nonfatal /r "${SourceDir}\styles\*"
  
  SetOutPath "$INSTDIR\imageformats"
  File /nonfatal /r "${SourceDir}\imageformats\*"
  
  SetOutPath "$INSTDIR\gamepads"
  File /nonfatal /r "${SourceDir}\gamepads\*"

  SetOutPath "$INSTDIR\mediaservice"
  File /nonfatal /r "${SourceDir}\mediaservice\*"
  
  ; Directories
  SetOutPath "$INSTDIR\data"
  File /r "${SourceDir}\data\*"
  
  SetOutPath "$INSTDIR\locales"
  File /r "${SourceDir}\locales\*"
  
  ; Config files
  SetOutPath "$INSTDIR"
  File /nonfatal "${SourceDir}\licence.txt"
  File /nonfatal "${SourceDir}\login_config.txt"
  
  ; Create uninstaller
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  
  ; Registry entries
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${MyAppName}" "DisplayName" "${MyAppName}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${MyAppName}" "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${MyAppName}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${MyAppName}" "Publisher" "${MyAppPublisher}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${MyAppName}" "URLInfoAbout" "${MyAppURL}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${MyAppName}" "DisplayVersion" "${MyAppVersion}"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${MyAppName}" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${MyAppName}" "NoRepair" 1
SectionEnd

Section "Desktop Shortcut" SecDesktop
  CreateShortcut "$DESKTOP\${MyAppName}.lnk" "$INSTDIR\${MyAppExeName}"
SectionEnd

Section "Start Menu Shortcut" SecStartMenu
  CreateDirectory "$SMPROGRAMS\${MyAppName}"
  CreateShortcut "$SMPROGRAMS\${MyAppName}\${MyAppName}.lnk" "$INSTDIR\${MyAppExeName}"
  CreateShortcut "$SMPROGRAMS\${MyAppName}\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

; Section Descriptions
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "Main application files (required)"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Create desktop shortcut"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} "Create start menu shortcuts"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; Uninstaller Section
Section "Uninstall"
  ; Remove files
  Delete "$INSTDIR\${MyAppExeName}"
  Delete "$INSTDIR\server.exe"
  Delete "$INSTDIR\browser_process.exe"
  Delete "$INSTDIR\Qt5Core.dll"
  Delete "$INSTDIR\Qt5Gui.dll"
  Delete "$INSTDIR\Qt5Widgets.dll"
  Delete "$INSTDIR\Qt5OpenGL.dll"
  Delete "$INSTDIR\Qt5Multimedia.dll"
  Delete "$INSTDIR\Qt5MultimediaWidgets.dll"
  Delete "$INSTDIR\Qt5Network.dll"
  Delete "$INSTDIR\Qt5Gamepad.dll"
  Delete "$INSTDIR\libcef.dll"
  Delete "$INSTDIR\chrome_elf.dll"
  Delete "$INSTDIR\icudtl.dat"
  Delete "$INSTDIR\resources.pak"
  Delete "$INSTDIR\v8_context_snapshot.bin"
  Delete "$INSTDIR\chrome_100_percent.pak"
  Delete "$INSTDIR\chrome_200_percent.pak"
  Delete "$INSTDIR\d3dcompiler_47.dll"
  Delete "$INSTDIR\libEGL.dll"
  Delete "$INSTDIR\libGLESv2.dll"
  Delete "$INSTDIR\vulkan-1.dll"
  Delete "$INSTDIR\vk_swiftshader.dll"
  Delete "$INSTDIR\vk_swiftshader_icd.json"
  Delete "$INSTDIR\SDL2.dll"
  Delete "$INSTDIR\dxcompiler.dll"
  Delete "$INSTDIR\dxil.dll"
  Delete "$INSTDIR\licence.txt"
  Delete "$INSTDIR\login_config.txt"
  Delete "$INSTDIR\Uninstall.exe"
  
  ; Remove directories
  RMDir /r "$INSTDIR\data"
  RMDir /r "$INSTDIR\locales"
  RMDir /r "$INSTDIR\platforms"
  RMDir /r "$INSTDIR\styles"
  RMDir /r "$INSTDIR\imageformats"
  RMDir /r "$INSTDIR\gamepads"
  RMDir /r "$INSTDIR\mediaservice"
  
  ; Remove shortcuts
  Delete "$DESKTOP\${MyAppName}.lnk"
  RMDir /r "$SMPROGRAMS\${MyAppName}"
  
  ; Remove registry entries
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${MyAppName}"
  
  ; Remove installation directory if empty
  RMDir "$INSTDIR"
SectionEnd
