# PowerShell script to create NSIS installer for Metasiberia Beta
# Requires NSIS to be installed and in PATH

param(
    [string]$SourceDir = "C:\programming\substrata_output_qt\vs2022\cyberspace_x64\Release",
    [string]$OutputDir = "C:\programming\substrata_output_qt\installers",
    [string]$Version = "0.0.2",
    [string]$NSISPath = "makensis"
)

Write-Host "Creating NSIS installer for Metasiberia Beta v$Version..."
Write-Host ""

# Check if NSIS is available
if (Test-Path $NSISPath) {
    $nsisExePath = $NSISPath
} else {
    $nsisExe = Get-Command $NSISPath -ErrorAction SilentlyContinue
    if ($nsisExe) {
        $nsisExePath = $nsisExe.Path
    } else {
        Write-Host "ERROR: NSIS not found. Please install NSIS and add it to PATH, or specify path with -NSISPath parameter"
        Write-Host "Example: -NSISPath 'C:\Program Files (x86)\NSIS\makensis.exe'"
        exit 1
    }
}

# Check source directory
if (-not (Test-Path $SourceDir)) {
    Write-Host "ERROR: Source directory not found: $SourceDir"
    exit 1
}

# Check if gui_client.exe exists
if (-not (Test-Path "$SourceDir\gui_client.exe")) {
    Write-Host "ERROR: gui_client.exe not found in $SourceDir"
    Write-Host "Please build the project first"
    exit 1
}

# Create output directory
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

# Get script directory
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$nsiFile = Join-Path $scriptDir "create_installer.nsi"

# Check if NSI file exists
if (-not (Test-Path $nsiFile)) {
    Write-Host "ERROR: NSIS script not found: $nsiFile"
    exit 1
}

# Use original NSI file - all values passed via command line (/D params)
# Avoid temp file to prevent UTF-8 BOM corruption that causes NSIS integrity check failure

Write-Host "Source directory: $SourceDir"
Write-Host "Output directory: $OutputDir"
Write-Host "Version: $Version"
Write-Host "NSIS executable: $nsisExePath"
Write-Host ""

# Check if license file exists
$licenseFile = Join-Path $SourceDir "licence.txt"
$licenseParam = ""
if (Test-Path $licenseFile) {
    $licenseParam = "/DLICENSE_FILE=`"$licenseFile`""
}

# Compile installer
Write-Host "Compiling NSIS installer..."
$params = @("/DMyAppVersion=$Version", "/DSourceDir=$SourceDir", "/DOutputDir=$OutputDir")
if ($licenseParam) {
    $params += $licenseParam
}
$params += $nsiFile
& $nsisExePath $params

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "Installer created successfully!"
    Write-Host "Output: $OutputDir\MetasiberiaBeta-Setup-v$Version.exe"
} else {
    Write-Host ""
    Write-Host "ERROR: NSIS compilation failed with exit code $LASTEXITCODE"
    exit 1
}
