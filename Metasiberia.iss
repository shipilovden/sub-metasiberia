[Setup]
AppName=Metasiberia
AppVersion=1.0.0
AppPublisher=Denis Shipilov
AppPublisherURL=https://x.com/denshipilovart
AppSupportURL=https://x.com/denshipilovart
AppUpdatesURL=https://x.com/denshipilovart
AppCopyright=Copyright (C) 2025 Denis Shipilov
VersionInfoCompany=Metasiberia
VersionInfoDescription=Metasiberia Setup
VersionInfoVersion=1.0.0.0
VersionInfoTextVersion=1.0.0
VersionInfoProductName=Metasiberia
VersionInfoProductVersion=1.0.0.0
VersionInfoCopyright=Copyright (C) 2025 Denis Shipilov
VersionInfoOriginalFilename=Metasiberia_v1.0.0_Setup.exe
DefaultDirName={autopf}\Metasiberia
DefaultGroupName=Metasiberia
AllowNoIcons=yes
OutputDir=C:\programming\substrata_output\installers
OutputBaseFilename=Metasiberia_v1.0.0_Setup
SetupIconFile=C:\programming\substrata\icons\substrata.ico
Compression=lzma
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
DisableWelcomePage=no
DisableDirPage=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "quicklaunchicon"; Description: "{cm:CreateQuickLaunchIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked; OnlyBelowVersion: 6.1

[Files]
Source: "C:\programming\substrata_output\vs2022\cyberspace_x64\RelWithDebInfo\gui_client.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\programming\substrata_output\vs2022\cyberspace_x64\RelWithDebInfo\*.dll"; DestDir: "{app}"; Flags: ignoreversion; Excludes: "chrome_elf.dll,libcef.dll,libEGL.dll,libGLESv2.dll"
Source: "C:\programming\substrata_output\vs2022\cyberspace_x64\RelWithDebInfo\*.lib"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\programming\substrata_output\vs2022\cyberspace_x64\RelWithDebInfo\*.pdb"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\programming\substrata_output\vs2022\cyberspace_x64\RelWithDebInfo\*.exe"; DestDir: "{app}"; Flags: ignoreversion; Excludes: "gui_client.exe"
Source: "C:\programming\substrata_output\vs2022\cyberspace_x64\RelWithDebInfo\*.bin"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\programming\substrata_output\vs2022\cyberspace_x64\RelWithDebInfo\platforms\*"; DestDir: "{app}\platforms"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "C:\programming\substrata_output\vs2022\cyberspace_x64\RelWithDebInfo\styles\*"; DestDir: "{app}\styles"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "C:\programming\substrata_output\vs2022\cyberspace_x64\RelWithDebInfo\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "C:\programming\substrata_output\vs2022\cyberspace_x64\RelWithDebInfo\resources\*"; DestDir: "{app}\resources"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "C:\programming\substrata_output\vs2022\cyberspace_x64\RelWithDebInfo\translations\*"; DestDir: "{app}\translations"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Metasiberia"; Filename: "{app}\gui_client.exe"; WorkingDir: "{app}"
Name: "{group}\{cm:UninstallProgram,Metasiberia}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Metasiberia"; Filename: "{app}\gui_client.exe"; Tasks: desktopicon; WorkingDir: "{app}"
Name: "{userappdata}\Microsoft\Internet Explorer\Quick Launch\Metasiberia"; Filename: "{app}\gui_client.exe"; Tasks: quicklaunchicon; WorkingDir: "{app}"

[Run]
Filename: "{app}\gui_client.exe"; Description: "{cm:LaunchProgram,Metasiberia}"; Flags: nowait postinstall skipifsilent; WorkingDir: "{app}"

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
