; Inno Setup Script for Metasiberia Beta
; Creates installer with web-based UI using CEF

; Version can be overridden via command line: /DMyAppVersion="0.0.0.1"
; Metasiberia uses semantic versioning format (e.g., "0.0.0.1", "0.0.0.2", "0.1.0.0", etc.)
#ifndef MyAppVersion
  #define MyAppVersion "0.0.0.1"
#endif

#define MyAppName "Metasiberia SDL beta"
#define MyAppPublisher "Metasiberia"
#define MyAppAuthor "Denis Shipilov"
#define MyAppURL "https://vr.metasiberia.com/"
#define MyAppExeName "gui_client.exe"
#define SourceDir "C:\programming\substrata_output\vs2022\cyberspace_x64\RelWithDebInfo"
#define OutputDir "C:\programming\substrata_output\installers"

[Setup]
; NOTE: The value of AppId uniquely identifies this application. Do not use the same AppId value in installers for other applications.
AppId={{A1B2C3D4-E5F6-4A5B-8C9D-0E1F2A3B4C5D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
LicenseFile={#SourceDir}\licence.txt
; Uncomment the following line to run in non administrative install mode (install for current user only.)
;PrivilegesRequired=lowest
PrivilegesRequired=admin
OutputDir={#OutputDir}
OutputBaseFilename=MetasiberiaBeta-Setup-v{#MyAppVersion}
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription=Metasiberia Beta Installer by {#MyAppAuthor}
VersionInfoCopyright=Copyright (C) 2026 {#MyAppPublisher}
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}
VersionInfoProductTextVersion={#MyAppVersion}
VersionInfoTextVersion={#MyAppVersion}
VersionInfoOriginalFilename=MetasiberiaBeta-Setup-v{#MyAppVersion}.exe
SetupIconFile=
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64
ArchitecturesAllowed=x64

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "quicklaunchicon"; Description: "{cm:CreateQuickLaunchIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked; OnlyBelowVersion: 6.1; Check: not IsAdminInstallMode

[Files]
; Main executable
Source: "{#SourceDir}\gui_client.exe"; DestDir: "{app}"; Flags: ignoreversion; BeforeInstall: RenameToMetasiberiaBeta
Source: "{#SourceDir}\server.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\browser_process.exe"; DestDir: "{app}"; Flags: ignoreversion

; CEF files
Source: "{#SourceDir}\libcef.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\chrome_elf.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\icudtl.dat"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\resources.pak"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\v8_context_snapshot.bin"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\chrome_100_percent.pak"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\chrome_200_percent.pak"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\d3dcompiler_47.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\libEGL.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\libGLESv2.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\vulkan-1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\vk_swiftshader.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\vk_swiftshader_icd.json"; DestDir: "{app}"; Flags: ignoreversion

; SDL
Source: "{#SourceDir}\SDL2.dll"; DestDir: "{app}"; Flags: ignoreversion

; Visual C++ Redistributable
Source: "{#SourceDir}\vcruntime140.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\msvcp140.dll"; DestDir: "{app}"; Flags: ignoreversion

; DirectX
Source: "{#SourceDir}\dxcompiler.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\dxil.dll"; DestDir: "{app}"; Flags: ignoreversion

; Directories
Source: "{#SourceDir}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\locales\*"; DestDir: "{app}\locales"; Flags: ignoreversion recursesubdirs createallsubdirs
; Note: swiftshader directory may be empty, so we skip it if it doesn't have files
Source: "{#SourceDir}\swiftshader\*"; DestDir: "{app}\swiftshader"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#SourceDir}\Resources\*"; DestDir: "{app}\Resources"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; Config files
Source: "{#SourceDir}\login_config.txt"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SourceDir}\licence.txt"; DestDir: "{app}"; Flags: ignoreversion

; NOTE: Don't use "Flags: ignoreversion" on any shared system files

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
Name: "{userappdata}\Microsoft\Internet Explorer\Quick Launch\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: quicklaunchicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Code]
procedure RenameToMetasiberiaBeta();
var
  SourceFile: string;
  DestFile: string;
begin
  SourceFile := ExpandConstant('{app}\gui_client.exe');
  DestFile := ExpandConstant('{app}\Metasiberia SDL beta.exe');
  if FileExists(SourceFile) then
  begin
    RenameFile(SourceFile, DestFile);
  end;
end;

