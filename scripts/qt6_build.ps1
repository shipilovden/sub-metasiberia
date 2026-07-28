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
            "bin\windeployqt.exe",
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

function Get-QtPluginDirectoryNames([string]$QtRoot) {
    $names = @(
        "bearer", "gamepads", "mediaservice", "platforminputcontexts",
        "platforms", "styles", "imageformats", "iconengines", "generic",
        "multimedia", "networkinformation", "sqldrivers", "tls"
    )
    $sourcePluginRoot = Join-Path $QtRoot "plugins"
    if (Test-Path -LiteralPath $sourcePluginRoot -PathType Container) {
        $names += @(Get-ChildItem -LiteralPath $sourcePluginRoot -Directory | Select-Object -ExpandProperty Name)
    }
    return @($names | Sort-Object -Unique)
}

function Assert-PathUnderRoot([string]$Path, [string]$Root, [string]$Label) {
    $directorySeparators = [char[]]@([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd($directorySeparators)
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd($directorySeparators)
    $rootPrefix = $fullRoot + [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must be below '$fullRoot', got '$fullPath'."
    }
}

function Remove-StaleQtRuntime([string]$RuntimeDir, [string]$QtRoot) {
    Require-Directory $RuntimeDir "Runtime output"
    Assert-PathUnderRoot $RuntimeDir $OutputRoot "Runtime output"

    # copy_files_to_output.rb also serves the Qt 5 workflow and may copy its
    # DLLs/plugins here. Remove the complete Qt runtime surface before asking
    # the selected Qt 6 SDK to deploy a coherent replacement.
    Get-ChildItem -LiteralPath $RuntimeDir -Recurse -File -Filter "Qt5*.dll" |
        ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }
    Get-ChildItem -LiteralPath $RuntimeDir -File -Filter "Qt6*.dll" |
        ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }

    foreach ($pluginDirectoryName in (Get-QtPluginDirectoryNames $QtRoot)) {
        $pluginDirectory = Join-Path $RuntimeDir $pluginDirectoryName
        if (Test-Path -LiteralPath $pluginDirectory -PathType Container) {
            Assert-PathUnderRoot $pluginDirectory $RuntimeDir "Qt plugin directory"
            Remove-Item -LiteralPath $pluginDirectory -Recurse -Force
        }
    }
}

function Deploy-Qt6Runtime([string]$Config, [string]$QtRoot) {
    $runtimeDir = Join-Path $OutputRoot "vs2022\cyberspace_x64\$Config"
    $guiClient = Join-Path $runtimeDir "gui_client.exe"
    $windeployqt = Join-Path $QtRoot "bin\windeployqt.exe"
    Require-File $guiClient "Qt 6 gui_client executable"
    Require-File $windeployqt "Qt 6 deployment tool"

    Remove-StaleQtRuntime $runtimeDir $QtRoot
    $deployMode = if ($Config -eq "Debug") { "--debug" } else { "--release" }
    $requiredQtModules = @(
        "--core", "--gui", "--widgets", "--network", "--opengl",
        "--openglwidgets", "--multimedia", "--multimediawidgets", "--core5compat"
    )
    $deployArguments = @(
        $deployMode, "--force", "--no-system-d3d-compiler", "--no-system-dxc-compiler",
        "--dir", $runtimeDir
    ) + $requiredQtModules + $guiClient
    Invoke-Native $windeployqt $deployArguments "Qt 6 runtime deployment failed for $Config"
}

function Assert-Qt6RuntimeFile([string]$RuntimePath, [string]$SourcePath, [version]$ExpectedVersion, [string]$Label) {
    Require-File $RuntimePath $Label
    Require-File $SourcePath "$Label source"

    $versionInfo = [Diagnostics.FileVersionInfo]::GetVersionInfo($RuntimePath)
    $actualVersion = "$($versionInfo.FileMajorPart).$($versionInfo.FileMinorPart).$($versionInfo.FileBuildPart)"
    $expected = "$($ExpectedVersion.Major).$($ExpectedVersion.Minor).$($ExpectedVersion.Build)"
    if ($actualVersion -ne $expected -or $versionInfo.FileMajorPart -ne 6) {
        throw "$Label has Qt version $actualVersion, expected Qt ${expected}: $RuntimePath"
    }

    $runtimeHash = (Get-FileHash -LiteralPath $RuntimePath -Algorithm SHA256).Hash
    $sourceHash = (Get-FileHash -LiteralPath $SourcePath -Algorithm SHA256).Hash
    if ($runtimeHash -ne $sourceHash) {
        throw "$Label does not match the selected Qt 6 SDK (SHA-256 mismatch): $RuntimePath"
    }
}

function Validate-Runtime([string]$Config, [bool]$XREnabled, [string]$QtRoot, [version]$ExpectedQtVersion) {
    $dir = Join-Path $OutputRoot "vs2022\cyberspace_x64\$Config"
    $debugSuffix = if ($Config -eq "Debug") { "d" } else { "" }
    $qtModuleNames = @("Core", "Gui", "OpenGL", "OpenGLWidgets", "Widgets", "Multimedia", "MultimediaWidgets", "Network", "Core5Compat")
    $qtRuntimeFiles = @($qtModuleNames | ForEach-Object { "Qt6$($_)$debugSuffix.dll" })
    $platformPlugin = "platforms\qwindows$debugSuffix.dll"
    $required = @(
        "gui_client.exe", "browser_process.exe",
        "libcef.dll", "chrome_elf.dll", "libEGL.dll", "libGLESv2.dll",
        "resources.pak", "locales\en-US.pak"
    ) + $qtRuntimeFiles + $platformPlugin
    if ($XREnabled) { $required += "openxr_loader.dll" }
    $missing = @($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $dir $_) -PathType Leaf) })
    if ($missing.Count -gt 0) { throw "Qt 6 runtime validation failed for $Config. Missing: $($missing -join '; ')" }

    $qt5Artifacts = @(Get-ChildItem -LiteralPath $dir -Recurse -File -Filter "*.dll" | Where-Object {
        $info = $_.VersionInfo
        $_.Name -like "Qt5*.dll" -or
            $info.ProductName -like "Qt5*" -or
            ($info.FileMajorPart -eq 5 -and $info.CompanyName -match "Qt")
    })
    if ($qt5Artifacts.Count -gt 0) {
        $paths = @($qt5Artifacts | ForEach-Object { $_.FullName.Substring($dir.Length).TrimStart([IO.Path]::DirectorySeparatorChar) })
        throw "Qt 6 runtime validation failed for $Config. Qt 5 artifacts remain: $($paths -join '; ')"
    }

    foreach ($relativePath in $qtRuntimeFiles) {
        Assert-Qt6RuntimeFile (Join-Path $dir $relativePath) (Join-Path (Join-Path $QtRoot "bin") $relativePath) $ExpectedQtVersion $relativePath
    }
    Assert-Qt6RuntimeFile (Join-Path $dir $platformPlugin) (Join-Path (Join-Path $QtRoot "plugins") $platformPlugin) $ExpectedQtVersion $platformPlugin

    foreach ($runtimeQtLibrary in (Get-ChildItem -LiteralPath $dir -File -Filter "Qt6*.dll")) {
        $sourceQtLibrary = Join-Path (Join-Path $QtRoot "bin") $runtimeQtLibrary.Name
        Assert-Qt6RuntimeFile $runtimeQtLibrary.FullName $sourceQtLibrary $ExpectedQtVersion $runtimeQtLibrary.Name
    }

    foreach ($pluginDirectoryName in (Get-QtPluginDirectoryNames $QtRoot)) {
        $runtimePluginDirectory = Join-Path $dir $pluginDirectoryName
        if (-not (Test-Path -LiteralPath $runtimePluginDirectory -PathType Container)) { continue }
        foreach ($plugin in (Get-ChildItem -LiteralPath $runtimePluginDirectory -Recurse -File -Filter "*.dll")) {
            $relativePluginPath = $plugin.FullName.Substring($dir.Length).TrimStart([IO.Path]::DirectorySeparatorChar)
            $sourcePluginPath = Join-Path (Join-Path $QtRoot "plugins") $relativePluginPath
            Assert-Qt6RuntimeFile $plugin.FullName $sourcePluginPath $ExpectedQtVersion $relativePluginPath
        }
    }
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
    $expectedQtVersion = [version]$QtVersion
    if ($expectedQtVersion.Major -ne 6 -or $expectedQtVersion.Build -lt 0) {
        throw "QtVersion must identify a three-part Qt 6 release, got '$QtVersion'."
    }
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
            Deploy-Qt6Runtime $config $qtResolved
            Validate-Runtime $config $xrResolved.Enabled $qtResolved $expectedQtVersion
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
