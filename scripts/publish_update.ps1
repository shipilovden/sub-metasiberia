# Publishes a new Metasiberia Beta update:
# - bumps version in shared/Version.h
# - commits + tags
# - builds Qt client (qt_build.ps1)
# - creates installer (create_simple_installer.ps1)
# - creates GitHub Release + uploads installer asset

param(
    [ValidateSet("patch", "minor", "major", "none")]
    [string]$Bump = "patch",
    [ValidateSet("Release", "RelWithDebInfo")]
    [string]$Config = "Release",
    [switch]$Prerelease = $true,
    [string]$Repo = "shipilovden/sub-metasiberia",
    [string]$Remote = "myorigin",
    [string]$AppName = "Metasiberia Beta",
    [string]$Notes = "Installer update.",
    [switch]$DryRun = $false
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Exec {
    param([string]$Cmd)
    Write-Host $Cmd
    if(-not $DryRun) {
        & powershell -NoProfile -ExecutionPolicy Bypass -Command $Cmd
        if($LASTEXITCODE -ne 0) { throw ("Command failed with exit code {0}: {1}" -f $LASTEXITCODE, $Cmd) }
    }
}

function ExecNative {
    param([string]$File, [string[]]$Arguments)
    $argStr = ($Arguments | ForEach-Object { if($_ -match "\\s") { '\"' + $_ + '\"' } else { $_ } }) -join " "
    Write-Host "$File $argStr"
    if(-not $DryRun) {
        & $File @Arguments
        if($LASTEXITCODE -ne 0) { throw ("Command failed with exit code {0}: {1} {2}" -f $LASTEXITCODE, $File, $argStr) }
    }
}

function Require-Path {
    param(
        [string]$Path,
        [bool]$AllowMissingOnDryRun = $false
    )
    if($DryRun -and $AllowMissingOnDryRun) { return }
    if(-not (Test-Path $Path)) { throw "Missing path: $Path" }
}

function Get-RepoRoot {
    $p = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
    return $p
}

function Get-VersionFromHeader {
    param([string]$HeaderPath)
    $line = Select-String -Path $HeaderPath -Pattern 'cyberspace_version\s*=\s*"([^"]+)"' | Select-Object -First 1
    if(-not $line) { throw "Could not parse cyberspace_version from $HeaderPath" }
    return $line.Matches[0].Groups[1].Value
}

function Set-VersionInHeader {
    param([string]$HeaderPath, [string]$NewVersion)
    $text = Get-Content -Raw -Path $HeaderPath
    $newText = [Regex]::Replace($text, '(cyberspace_version\s*=\s*")([^"]+)(";)', {
        param($m)
        return ($m.Groups[1].Value + $NewVersion + $m.Groups[3].Value)
    })
    if($newText -eq $text) { throw "Failed to update version in $HeaderPath" }
    if(-not $DryRun) {
        # Write UTF-8 without BOM to avoid introducing hidden characters in Version.h.
        [System.IO.File]::WriteAllText($HeaderPath, $newText, (New-Object System.Text.UTF8Encoding($false)))
    }
}

function Parse-SemVer {
    param([string]$V)
    $m = [Regex]::Match($V, '^(\d+)\.(\d+)\.(\d+)$')
    if(-not $m.Success) { throw "Invalid version (expected X.Y.Z): $V" }
    return @{
        major = [int]$m.Groups[1].Value
        minor = [int]$m.Groups[2].Value
        patch = [int]$m.Groups[3].Value
    }
}

function Format-SemVer {
    param($Sem)
    return ("{0}.{1}.{2}" -f $Sem.major, $Sem.minor, $Sem.patch)
}

function Compute-NextVersion {
    param([string]$Current, [string]$BumpMode)
    if($BumpMode -eq "none") { return $Current }

    $s = Parse-SemVer $Current
    switch($BumpMode) {
        "patch" { $s.patch = $s.patch + 1 }
        "minor" { $s.minor = $s.minor + 1; $s.patch = 0 }
        "major" { $s.major = $s.major + 1; $s.minor = 0; $s.patch = 0 }
        default { throw "Unknown bump: $BumpMode" }
    }
    return (Format-SemVer $s)
}

function Ensure-CleanGit {
    $st = @(& git status --porcelain=v1)
    if($LASTEXITCODE -ne 0) { throw "git status failed" }
    if($st.Count -gt 0) {
        throw "Working tree is not clean. Commit/stash first."
    }
}

$repoRoot = Get-RepoRoot
Set-Location $repoRoot

Require-Path (Join-Path $repoRoot "shared\\Version.h")
Require-Path "C:\\programming\\qt_build.ps1"
Require-Path (Join-Path $repoRoot "scripts\\create_simple_installer.ps1")

Ensure-CleanGit

$versionHeader = Join-Path $repoRoot "shared\\Version.h"
$currentVersion = Get-VersionFromHeader -HeaderPath $versionHeader
$newVersion = Compute-NextVersion -Current $currentVersion -BumpMode $Bump

if($newVersion -eq $currentVersion) {
    throw "New version equals current version ($currentVersion). Choose -Bump patch/minor/major."
}

$tag = "v$newVersion"
$title = "$AppName $tag"

Write-Host "Current version: $currentVersion"
Write-Host "New version:     $newVersion"
Write-Host "Tag:             $tag"
Write-Host "Config:          $Config"
Write-Host "Prerelease:      $Prerelease"

Set-VersionInHeader -HeaderPath $versionHeader -NewVersion $newVersion

ExecNative "git" @("add", "shared/Version.h")
ExecNative "git" @("commit", "-m", "Bump version to $newVersion")
ExecNative "git" @("tag", $tag)
ExecNative "git" @("push", $Remote, "master")
ExecNative "git" @("push", $Remote, $tag)

# Build Qt output (writes to substrata_output_qt)
ExecNative "powershell" @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "C:\\programming\\qt_build.ps1")

# Always build Qt Debug as well (policy: keep Debug and Release artifacts in sync before publishing).
$env:GLARE_CORE_LIBS = "c:/programming"
$env:WINTER_DIR = "c:/programming/winter"
$env:GLARE_CORE_TRUNK_DIR = "c:/programming/glare-core"
$env:CEF_BINARY_DISTRIB_DIR = "C:\cef\chromium\src\cef\binary_distrib\cef_binary_139.0.40+g465474a+chromium-139.0.7258.139_windows64"
$env:INDIGO_QT_VERSION = "5.15.16-vs2022-64"
$env:CYBERSPACE_OUTPUT = "c:/programming/substrata_output_qt"

ExecNative "cmake" @("--build", "C:\\programming\\substrata_build_qt", "--config", "Debug", "--target", "gui_client", "-j", "8")
Push-Location (Join-Path $repoRoot "scripts")
try {
    ExecNative "ruby" @("copy_files_to_output.rb", "--no_bugsplat", "--config", "Debug")
}
finally {
    Pop-Location
}

$sourceDir = "C:\\programming\\substrata_output_qt\\vs2022\\cyberspace_x64\\$Config"
Require-Path (Join-Path $sourceDir "gui_client.exe") -AllowMissingOnDryRun $true

# Create installer (writes to substrata_output\\installers by default)
$env:CYBERSPACE_OUTPUT = "C:\\programming\\substrata_output_qt"
ExecNative "powershell" @(
    "-NoProfile", "-ExecutionPolicy", "Bypass",
    "-File", (Join-Path $repoRoot "scripts\\create_simple_installer.ps1"),
    "-Config", $Config,
    "-SourceDir", $sourceDir
)

$installerPath = "C:\\programming\\substrata_output\\installers\\MetasiberiaBeta-Setup-$tag.exe"
Require-Path $installerPath -AllowMissingOnDryRun $true

# Publish GitHub release + upload installer
$releaseArgs = @("release", "create", $tag, $installerPath, "-R", $Repo, "--title", $title, "--notes", $Notes, "--verify-tag")
if($Prerelease) { $releaseArgs += "--prerelease" }
ExecNative "gh" $releaseArgs

Write-Host ""
Write-Host "Published: $tag"
Write-Host "Installer: $installerPath"
