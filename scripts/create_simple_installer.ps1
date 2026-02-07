# Simple PowerShell-based installer creator
# Creates a self-extracting archive with installation script

param(
    [string]$SourceDir = "C:\programming\substrata_output\vs2022\cyberspace_x64\RelWithDebInfo",
    [string]$OutputDir = "C:\programming\substrata_output\installers",
    [string]$AppName = "Metasiberia Beta"
)

Write-Host "Creating installer for $AppName..."
Write-Host ""

# Check source directory
if (-not (Test-Path $SourceDir)) {
    Write-Host "ERROR: Source directory not found: $SourceDir"
    exit 1
}

# Create output directory
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

# Create installer script
$installScript = @"
@echo off
echo ========================================
echo $AppName - Installation
echo ========================================
echo.

set INSTALL_DIR=%ProgramFiles%\$AppName
set DESKTOP_DIR=%USERPROFILE%\Desktop

echo Installing to: %INSTALL_DIR%
echo.

REM Create installation directory
if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"

REM Copy files
echo Copying files...
xcopy /E /I /Y * "%INSTALL_DIR%\" >nul

REM Create desktop shortcut
echo Creating desktop shortcut...
set SHORTCUT=%DESKTOP_DIR%\$AppName.lnk
powershell -Command "$WshShell = New-Object -ComObject WScript.Shell; `$Shortcut = `$WshShell.CreateShortcut('%SHORTCUT%'); `$Shortcut.TargetPath = '%INSTALL_DIR%\gui_client.exe'; `$Shortcut.WorkingDirectory = '%INSTALL_DIR%'; `$Shortcut.Save()"

echo.
echo Installation complete!
echo.
pause
"@

# Create uninstaller script
$uninstallScript = @"
@echo off
echo ========================================
echo $AppName - Uninstallation
echo ========================================
echo.

set INSTALL_DIR=%ProgramFiles%\$AppName
set DESKTOP_DIR=%USERPROFILE%\Desktop

echo Removing installation...
if exist "%INSTALL_DIR%" (
    rd /S /Q "%INSTALL_DIR%"
    echo Installation directory removed.
)

echo Removing desktop shortcut...
if exist "%DESKTOP_DIR%\$AppName.lnk" (
    del "%DESKTOP_DIR%\$AppName.lnk"
    echo Desktop shortcut removed.
)

echo.
echo Uninstallation complete!
echo.
pause
"@

# Save scripts
$installScriptPath = Join-Path $OutputDir "install.bat"
$uninstallScriptPath = Join-Path $OutputDir "uninstall.bat"
$installScript | Out-File -FilePath $installScriptPath -Encoding ASCII
$uninstallScript | Out-File -FilePath $uninstallScriptPath -Encoding ASCII

Write-Host "Installer scripts created:"
Write-Host "  Install: $installScriptPath"
Write-Host "  Uninstall: $uninstallScriptPath"
Write-Host ""
Write-Host "To create installer:"
Write-Host "  1. Copy all files from $SourceDir to a folder"
Write-Host "  2. Copy install.bat and uninstall.bat to that folder"
Write-Host "  3. Create ZIP archive of the folder"
Write-Host "  4. Users extract and run install.bat"
Write-Host ""
Write-Host "Or use 7-Zip to create self-extracting archive:"
Write-Host "  7z a -sfx installer.exe $SourceDir\* install.bat uninstall.bat"
