# Canonical Qt 6 Windows build for the qt6-integration branch.
# The Qt 5 workflow remains C:\programming\qt_build.ps1 on master.

param(
    [ValidateSet("Release", "RelWithDebInfo", "Debug")]
    [string[]]$Configs = @("Release", "RelWithDebInfo"),
    [ValidateSet("Auto", "On", "Off")]
    [string]$XR = "Auto",
    [ValidateRange(1, 64)]
    [int]$Jobs = 8,
    [string]$QtVersion = "6.11.1",
    [string]$QtDir = "",
    [string]$RepoRoot = "C:\programming\substrata",
    [string]$BuildDir = "C:\programming\substrata_build_qt6",
    [string]$OutputRoot = "C:\programming\substrata_output_qt6",
    [string]$OpenXRSDKDir = "",
    [string]$CefDistribDir = "D:\cef\binary_distrib\cef_binary_139.0.40+g465474a+chromium-139.0.7258.139_windows64"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptsDir = Join-Path $RepoRoot "scripts"
$manifestPath = Join-Path $OutputRoot "build_manifest.json"
$configs = @($Configs | Select-Object -Unique)
$runId = [guid]::NewGuid().ToString("D")

function Require-Command([string]$Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) { throw "Required command not found: $Name" }
}

function Require-Directory([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) { throw "$Label not found: $Path" }
}

function Require-File([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "$Label not found: $Path" }
}

function Invoke-Native([string]$FilePath, [string[]]$Arguments, [string]$FailureMessage) {
    Write-Host ("> " + $FilePath + " " + ($Arguments -join " ")) -ForegroundColor DarkGray
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$FailureMessage (exit $LASTEXITCODE)." }
}

function Write-Manifest($Value) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $manifestPath) -Force | Out-Null
    $temporary = Join-Path (Split-Path -Parent $manifestPath) ".build_manifest.$runId.tmp"
    try {
        $Value | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $temporary -Encoding UTF8
        Move-Item -LiteralPath $temporary -Destination $manifestPath -Force
    }
    finally {
        if (Test-Path -LiteralPath $temporary -PathType Leaf) { Remove-Item -LiteralPath $temporary -Force }
    }
}

function Resolve-Qt6Dir {
    $candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($QtDir)) { $candidates += $QtDir }
    if (-not [string]::IsNullOrWhiteSpace($env:SUBSTRATA_QT_DIR)) { $candidates += $env:SUBSTRATA_QT_DIR }
    $candidates += @(
        "C:\programming\Qt\$QtVersion-vs2022-64",
        "C:\programming\Qt\$QtVersion\msvc2022_64",
        "C:\Qt\$QtVersion\msvc2022_64"
    )

    foreach ($candidate in $candidates | Select-Object -Unique) {
        $required = @(
            "include\QtCore\qglobal.h", "bin\moc.exe", "bin\uic.exe",
            "lib\Qt6Core.lib", "lib\Qt6Gui.lib", "lib\Qt6OpenGL.lib",
            "lib\Qt6OpenGLWidgets.lib", "lib\Qt6Widgets.lib",
            "lib\Qt6Multimedia.lib", "lib\Qt6MultimediaWidgets.lib",
            "lib\Qt6Network.lib", "lib\Qt6Core5Compat.lib",
            "bin\Qt6Core.dll", "plugins\platforms\qwindows.dll"
        )
        if ((Test-Path -LiteralPath $candidate -PathType Container) -and
            (@($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $candidate $_) -PathType Leaf) }).Count -eq 0)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $mingwCandidates = @($candidates | Where-Object { Test-Path -LiteralPath (Join-Path $_ "lib\libQt6Core.a") })
    if ($mingwCandidates.Count -gt 0) {
        throw "Found Qt 6 MinGW distribution at $($mingwCandidates -join '; '), but this Windows workflow requires Qt 6 MSVC 2022 because CEF and native dependencies use the Visual Studio ABI. Install the Qt 6.11.1 MSVC 2022 64-bit component; Qt 5 fallback is forbidden."
    }
    throw "Qt 6 $QtVersion MSVC 2022 distribution was not found. Install it or pass -QtDir <root>; Qt 5 fallback is forbidden. Checked: $($candidates -join '; ')"
}

function Resolve-XR {
    if ($XR -eq "Off") { return [pscustomobject]@{ Enabled = $false; Dir = "" } }
    $candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($OpenXRSDKDir)) { $candidates += $OpenXRSDKDir }
    $candidates += @("C:\programming\OpenXR-SDK-1.1.57\install", "C:\programming\OpenXR-SDK\install")
    foreach ($candidate in $candidates | Select-Object -Unique) {
        if (Test-Path -LiteralPath (Join-Path $candidate "cmake\OpenXRConfig.cmake") -PathType Leaf) {
            return [pscustomobject]@{ Enabled = $true; Dir = (Resolve-Path -LiteralPath $candidate).Path }
        }
    }
    if ($XR -eq "On") { throw "Qt 6 XR was requested, but OpenXR SDK was not found." }
    return [pscustomobject]@{ Enabled = $false; Dir = "" }
}

function Validate-Runtime([string]$Config, [bool]$XREnabled) {
    $dir = Join-Path $OutputRoot "vs2022\cyberspace_x64\$Config"
    $required = @(
        "gui_client.exe", "Qt6Core.dll", "Qt6Gui.dll", "Qt6OpenGL.dll",
        "Qt6OpenGLWidgets.dll", "Qt6Widgets.dll", "Qt6Multimedia.dll",
        "Qt6MultimediaWidgets.dll", "Qt6Network.dll",
        "Qt6Core5Compat.dll", "platforms\qwindows.dll", "browser_process.exe",
        "libcef.dll", "chrome_elf.dll", "libEGL.dll", "libGLESv2.dll",
        "resources.pak", "locales\en-US.pak"
    )
    if ($XREnabled) { $required += "openxr_loader.dll" }
    $missing = @($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $dir $_) -PathType Leaf) })
    if ($missing.Count -gt 0) { throw "Qt 6 runtime validation failed for $Config. Missing: $($missing -join '; ')" }
}

$manifest = [ordered]@{
    generated_at_utc = (Get-Date).ToUniversalTime().ToString("o")
    run_id = $runId; status = "running"; success = $false
    branch = ""; qt_version = $QtVersion; qt_dir = ""; build_dir = $BuildDir; output_root = $OutputRoot
    cef_support = $true; cef_dir = $CefDistribDir; xr_requested = $XR; builds = @()
}
Write-Manifest $manifest

try {
    Require-Command "cmake"; Require-Command "ruby"; Require-Command "git"
    Require-Directory $RepoRoot "Metasiberia repository"
    $branch = (& git -C $RepoRoot branch --show-current).Trim()
    if ($branch -ne "qt6-integration") { throw "Qt 6 wrapper must run on qt6-integration, current branch is '$branch'." }
    $manifest.branch = $branch
    $changes = @(& git -C $RepoRoot status --porcelain=v1)
    if ($changes.Count -gt 0) {
        $answer = Read-Host "qt6-integration has uncommitted changes. Continue? (y/N)"
        if ($answer -notin @("y", "Y", "yes", "Yes")) { throw "Build aborted due to dirty working tree." }
    }

    $qtResolved = Resolve-Qt6Dir
    $xrResolved = Resolve-XR
    Require-Directory $CefDistribDir "CEF binary distribution"
    Require-File (Join-Path $CefDistribDir "include\cef_app.h") "CEF header"
    Require-File (Join-Path $CefDistribDir "Release\libcef.lib") "CEF Release import library"
    Require-File (Join-Path $CefDistribDir "libcef_dll_build\libcef_dll_wrapper\Release\libcef_dll_wrapper.lib") "CEF Release wrapper"

    $env:SUBSTRATA_QT_VERSION = $QtVersion
    $env:SUBSTRATA_QT_DIR = $qtResolved
    $env:GLARE_CORE_LIBS = "C:/programming"
    $env:WINTER_DIR = "C:/programming/winter"
    $env:GLARE_CORE_TRUNK_DIR = "C:/programming/glare-core"
    $env:CYBERSPACE_OUTPUT = $OutputRoot
    $env:CEF_BINARY_DISTRIB_DIR = $CefDistribDir
    if ($xrResolved.Enabled) { $env:OPENXR_SDK_DIR = $xrResolved.Dir }

    $cmakeArgs = @("-S", $RepoRoot, "-B", $BuildDir, "-G", "Visual Studio 17 2022", "-A", "x64", "-DUSE_SDL=OFF", "-DBUGSPLAT_SUPPORT=OFF", "-DCEF_SUPPORT=ON", "-DXR_SUPPORT=$(if($xrResolved.Enabled){'ON'}else{'OFF'})", "-DCEF_BINARY_DISTRIB_DIR=$CefDistribDir")
    if ($xrResolved.Enabled) { $cmakeArgs += "-DOPENXR_SDK_DIR=$($xrResolved.Dir)" }
    Invoke-Native "cmake" $cmakeArgs "Qt 6 CMake configure failed"

    $records = @()
    foreach ($config in $configs) {
        $record = [ordered]@{ config = $config; status = "failed"; runtime_status = "pending" }
        try {
            Invoke-Native "cmake" @("--build", $BuildDir, "--config", $config, "--target", "gui_client", "-j", "$Jobs") "Qt 6 build failed for $config"
            Push-Location $scriptsDir
            try { Invoke-Native "ruby" @(".\copy_files_to_output.rb", "--no_bugsplat", "--config", $config) "Qt 6 runtime copy failed for $config" }
            finally { Pop-Location }
            Validate-Runtime $config $xrResolved.Enabled
            $record.status = "success"; $record.runtime_status = "validated"
        }
        catch { $record.error = $_.Exception.Message }
        $records += [pscustomobject]$record
    }
    $allSuccess = @($records | Where-Object { $_.status -ne "success" }).Count -eq 0
    $manifest.status = if ($allSuccess) { "success" } else { "failed" }
    $manifest.success = $allSuccess
    $manifest.qt_dir = $qtResolved
    $manifest.builds = $records
    Write-Manifest $manifest
    if (-not $allSuccess) { exit 1 }
}
catch {
    $manifest.status = "failed"
    $manifest.error = $_.Exception.Message
    Write-Manifest $manifest
    Write-Error $_.Exception.Message
    exit 1
}
