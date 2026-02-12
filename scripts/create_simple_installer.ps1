# Creates a Windows .exe installer for Metasiberia.
# Preferred backend: NSIS (keeps custom installer icon).
# Fallback backend: IExpress (system icon only).

param(
    [string]$SourceDir = "",
    [string]$OutputDir = "C:\programming\substrata_output_qt\installers",
    [string]$AppName = "Metasiberia Beta",
    [string]$Version = "",
    [ValidateSet("Release", "RelWithDebInfo")]
    [string]$Config = "Release"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-DefaultSourceDir {
    param([string]$PreferredConfig)

    $base = "C:\programming\substrata_output_qt\vs2022\cyberspace_x64"
    $preferred = Join-Path $base $PreferredConfig
    if (Test-Path (Join-Path $preferred "gui_client.exe")) { return $preferred }

    $fallbackConfig = if ($PreferredConfig -eq "Release") { "RelWithDebInfo" } else { "Release" }
    $fallback = Join-Path $base $fallbackConfig
    if (Test-Path (Join-Path $fallback "gui_client.exe")) { return $fallback }

    throw "Could not find gui_client.exe in '$preferred' or '$fallback'."
}

function Resolve-Version {
    param([string]$RequestedVersion)

    if (-not [string]::IsNullOrWhiteSpace($RequestedVersion)) {
        if ($RequestedVersion.StartsWith("v")) { return $RequestedVersion }
        return "v$RequestedVersion"
    }

    $versionHeader = "C:\programming\substrata\shared\Version.h"
    if (Test-Path $versionHeader) {
        $match = Select-String -Path $versionHeader -Pattern 'cyberspace_version\s*=\s*"([^"]+)"' | Select-Object -First 1
        if ($match -and $match.Matches.Count -gt 0) {
            return "v$($match.Matches[0].Groups[1].Value)"
        }
    }

    return "v0.0.0"
}

function Find-MakeNsis {
    $candidates = @(
        "C:\Program Files (x86)\NSIS\makensis.exe",
        "C:\Program Files\NSIS\makensis.exe"
    )

    foreach ($path in $candidates) {
        if (Test-Path $path) { return $path }
    }

    return $null
}

function New-NsisInstaller {
    param(
        [string]$MakeNsisPath,
        [string]$Source,
        [string]$OutFile,
        [string]$Name,
        [string]$VersionTag,
        [string]$TempDir
    )

    $iconPath = "C:\programming\substrata\icons\metasiberia.ico"
    if (-not (Test-Path $iconPath)) {
        $iconPath = "C:\programming\substrata\icons\substrata.ico"
    }

    if (-not (Test-Path $iconPath)) {
        throw "No installer icon found (metasiberia.ico/substrata.ico)."
    }

    $nsiPath = Join-Path $TempDir "metasiberia_installer.nsi"
    $escapedSource = $Source.Replace("\", "\\")
    $escapedOut = $OutFile.Replace("\", "\\")
    $escapedIcon = $iconPath.Replace("\", "\\")

    $nsi = @"
Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!include "Sections.nsh"

Name "$Name $VersionTag"
OutFile "$escapedOut"
RequestExecutionLevel admin
InstallDir "`$PROGRAMFILES64\$Name"
InstallDirRegKey HKLM "Software\$Name" "Install_Dir"

!define MUI_ICON "$escapedIcon"
!define MUI_UNICON "$escapedIcon"
!define MUI_FINISHPAGE_RUN "`$INSTDIR\\gui_client.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch $Name"
!define MUI_FINISHPAGE_RUN_NOTCHECKED

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

Section /o "Remove previous installed version" SEC_REMOVE_OLD
  ReadRegStr `$0 HKLM "Software\$Name" "Install_Dir"
  StrCmp `$0 "" old_done
  IfFileExists "`$0\\Uninstall.exe" 0 old_done
  ExecWait '"`$0\\Uninstall.exe" /S _?=`$0'
old_done:
SectionEnd

Section "Core files (required)" SEC_CORE
  SectionIn RO
  SetOutPath "`$INSTDIR"
  File /r "$escapedSource\\*"

  WriteRegStr HKLM "Software\$Name" "Install_Dir" "`$INSTDIR"
  WriteUninstaller "`$INSTDIR\\Uninstall.exe"
SectionEnd

Section /o "Create desktop shortcut" SEC_DESKTOP
  CreateShortcut "`$DESKTOP\$Name.lnk" "`$INSTDIR\\gui_client.exe" "" "`$INSTDIR\\gui_client.exe" 0
SectionEnd

Function .onInit
  !insertmacro SelectSection `${SEC_REMOVE_OLD}
  !insertmacro SelectSection `${SEC_DESKTOP}
FunctionEnd

Section "Uninstall"
  Delete "`$DESKTOP\$Name.lnk"

  RMDir /r "`$INSTDIR"
  DeleteRegKey HKLM "Software\$Name"
SectionEnd
"@

    $nsi | Out-File -FilePath $nsiPath -Encoding ASCII
    & $MakeNsisPath /V2 $nsiPath | Out-Null

    if (-not (Test-Path $OutFile)) {
        throw "NSIS did not produce installer: $OutFile"
    }
}

function New-IexpressInstaller {
    param(
        [string]$Source,
        [string]$OutFile,
        [string]$Name,
        [string]$TempDir
    )

    $workDir = Join-Path $TempDir "iexpress"
    New-Item -ItemType Directory -Path $workDir -Force | Out-Null

    Copy-Item -Path (Join-Path $Source "*") -Destination $workDir -Recurse -Force

    $installBat = @"
@echo off
set INSTALL_DIR=%ProgramFiles%\$Name
if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"
xcopy /E /I /Y "%~dp0*.*" "%INSTALL_DIR%\" >nul
powershell -NoProfile -ExecutionPolicy Bypass -Command "`$w = New-Object -ComObject WScript.Shell; `$s = `$w.CreateShortcut('%USERPROFILE%\Desktop\$Name.lnk'); `$s.TargetPath = '%INSTALL_DIR%\gui_client.exe'; `$s.WorkingDirectory = '%INSTALL_DIR%'; `$s.Save()"
echo Installation complete.
pause
"@
    $installPath = Join-Path $workDir "install.bat"
    $installBat | Out-File -FilePath $installPath -Encoding ASCII

    $sedPath = Join-Path $TempDir "installer.sed"
    $sourceBlock = "SourceFiles0=$workDir`r`n[SourceFiles0]`r`n"
    $stringBlock = "[Strings]`r`n"

    $idx = 0
    Get-ChildItem -Path $workDir -File | ForEach-Object {
        $token = "FILE$idx"
        $sourceBlock += "%$token%=`r`n"
        $stringBlock += "$token=$($_.Name)`r`n"
        $idx++
    }

    $sed = @"
[Version]
Class=IEXPRESS
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=1
HideExtractAnimation=1
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
RebootMode=N
InstallPrompt=
DisplayLicense=
FinishMessage=
TargetName=$OutFile
FriendlyName=$Name Installer
AppLaunched=install.bat
PostInstallCmd=<None>
AdminQuietInstCmd=install.bat
UserQuietInstCmd=install.bat
SourceFiles=SourceFiles
[SourceFiles]
$sourceBlock
$stringBlock
"@

    $sed | Out-File -FilePath $sedPath -Encoding ASCII
    & "$env:WINDIR\System32\iexpress.exe" /N /Q $sedPath | Out-Null

    if (-not (Test-Path $OutFile)) {
        throw "IExpress did not produce installer: $OutFile"
    }
}

if ([string]::IsNullOrWhiteSpace($SourceDir)) {
    $SourceDir = Resolve-DefaultSourceDir -PreferredConfig $Config
}

if (-not (Test-Path (Join-Path $SourceDir "gui_client.exe"))) {
    throw "Source directory does not contain gui_client.exe: $SourceDir"
}

$versionTag = Resolve-Version -RequestedVersion $Version
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

$installerFileName = "MetasiberiaBeta-Setup-$versionTag.exe"
$installerPath = Join-Path $OutputDir $installerFileName

$tempRoot = Join-Path $env:TEMP ("metasiberia_installer_" + [DateTime]::Now.ToString("yyyyMMdd_HHmmss"))
New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null

try {
    Write-Host "Creating installer for $AppName ($versionTag)..."
    Write-Host "Source: $SourceDir"
    Write-Host "Output: $installerPath"
    Write-Host ""

    if (Test-Path $installerPath) {
        Remove-Item -Force $installerPath
    }

    $makensis = Find-MakeNsis
    if ($makensis) {
        Write-Host "Backend: NSIS ($makensis)"
        New-NsisInstaller -MakeNsisPath $makensis -Source $SourceDir -OutFile $installerPath -Name $AppName -VersionTag $versionTag -TempDir $tempRoot
    }
    else {
        Write-Host "Backend: IExpress (NSIS not found, icon will be generic)"
        New-IexpressInstaller -Source $SourceDir -OutFile $installerPath -Name $AppName -TempDir $tempRoot
    }

    $artifact = Get-Item $installerPath
    Write-Host ""
    Write-Host "Installer created successfully:"
    Write-Host "  $($artifact.FullName)"
    Write-Host "  Size: $([Math]::Round($artifact.Length / 1MB, 2)) MB"
    Write-Host "  Modified: $($artifact.LastWriteTime)"
}
finally {
    if (Test-Path $tempRoot) {
        Remove-Item -Path $tempRoot -Recurse -Force
    }
}
