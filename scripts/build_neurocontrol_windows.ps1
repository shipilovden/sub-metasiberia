param(
    [ValidateSet('OFF', 'ON')]
    [string]$FeatureSet = 'OFF',
    [string]$BuildDirectory,
    [string]$OutputRoot,
    [string]$OpenXrSdk = 'C:\programming\OpenXR-SDK-1.1.57\install',
    [string]$CefDistribution = 'D:\cef\binary_distrib\cef_binary_139.0.40+g465474a+chromium-139.0.7258.139_windows64',
    [string]$LogDirectory
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$programmingRoot = 'C:\programming'
$activeExe = Join-Path $programmingRoot 'substrata_output_qt\vs2022\cyberspace_x64\RelWithDebInfo\gui_client.exe'
$activeExeExpectedSha256 = 'C7A1D7CF81214DB282A1300E3BCCEFDDEFD34984FB7EBD3858BCE92AEEB149D4'
$featureKey = $FeatureSet.ToLowerInvariant()
$label = 'integration'

if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $programmingRoot "substrata_nc_build_${featureKey}_${label}"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $programmingRoot "substrata_nc_output_${featureKey}_${label}"
}
if ([string]::IsNullOrWhiteSpace($LogDirectory)) {
    $LogDirectory = Join-Path $repoRoot 'local_backups\build_logs'
}

function Normalize-AbsolutePath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
}

function Test-PathWithin([string]$Candidate, [string]$Root) {
    return $Candidate.Equals($Root, [StringComparison]::OrdinalIgnoreCase) -or
        $Candidate.StartsWith("$Root\", [StringComparison]::OrdinalIgnoreCase)
}

$BuildDirectory = Normalize-AbsolutePath $BuildDirectory
$OutputRoot = Normalize-AbsolutePath $OutputRoot
$repoRoot = Normalize-AbsolutePath $repoRoot
$programmingRoot = Normalize-AbsolutePath $programmingRoot
$sharedOutput = Normalize-AbsolutePath (Join-Path $programmingRoot 'substrata_output')
$activeOutput = Normalize-AbsolutePath (Join-Path $programmingRoot 'substrata_output_qt')

if ($OutputRoot -notmatch '^C:\\programming\\substrata_nc_output_(off|on)_[A-Za-z0-9_-]+$') {
    throw "Unsafe output root '$OutputRoot'. Use C:\programming\substrata_nc_output_${featureKey}_<label>."
}
if ($BuildDirectory -notmatch '^C:\\programming\\substrata_nc_build_(off|on)_[A-Za-z0-9_-]+$') {
    throw "Unsafe build directory '$BuildDirectory'. Use C:\programming\substrata_nc_build_${featureKey}_<label>."
}
if (($FeatureSet -eq 'ON' -and $OutputRoot -notmatch 'substrata_nc_output_on_') -or
    ($FeatureSet -eq 'OFF' -and $OutputRoot -notmatch 'substrata_nc_output_off_')) {
    throw "FeatureSet $FeatureSet does not match the selected output root."
}
if (($FeatureSet -eq 'ON' -and $BuildDirectory -notmatch 'substrata_nc_build_on_') -or
    ($FeatureSet -eq 'OFF' -and $BuildDirectory -notmatch 'substrata_nc_build_off_')) {
    throw "FeatureSet $FeatureSet does not match the selected build directory."
}
if ($BuildDirectory -ieq $OutputRoot -or $BuildDirectory.StartsWith("$OutputRoot\", [StringComparison]::OrdinalIgnoreCase) -or
    $OutputRoot.StartsWith("$BuildDirectory\", [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Build and output directories must be separate and non-nested.'
}
foreach ($protectedPath in @($repoRoot, $sharedOutput, $activeOutput, (Join-Path $programmingRoot 'substrata'), (Join-Path $programmingRoot 'Metasiberia official\substrata'))) {
    $protectedNormalized = Normalize-AbsolutePath $protectedPath
    if ($BuildDirectory.Equals($protectedNormalized, [StringComparison]::OrdinalIgnoreCase) -or
        $BuildDirectory.StartsWith("$protectedNormalized\", [StringComparison]::OrdinalIgnoreCase) -or
        $OutputRoot.Equals($protectedNormalized, [StringComparison]::OrdinalIgnoreCase) -or
        $OutputRoot.StartsWith("$protectedNormalized\", [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing build/output path under protected location '$protectedNormalized'."
    }
}
if (-not (Test-Path -LiteralPath $OpenXrSdk -PathType Container)) { throw "OpenXR SDK not found: $OpenXrSdk" }
if (-not (Test-Path -LiteralPath $CefDistribution -PathType Container)) { throw "CEF distribution not found: $CefDistribution" }
if (-not (Test-Path -LiteralPath $activeExe -PathType Leaf)) { throw "Control EXE not found: $activeExe" }
$beforeHash = (Get-FileHash -LiteralPath $activeExe -Algorithm SHA256).Hash
if ($beforeHash -ne $activeExeExpectedSha256) { throw "Active client hash differs from the approved control value: $beforeHash" }

$targetExe = Join-Path $OutputRoot 'vs2022\cyberspace_x64\RelWithDebInfo\gui_client.exe'
$runningTarget = Get-CimInstance Win32_Process -Filter "Name = 'gui_client.exe'" | Where-Object { $_.ExecutablePath -and $_.ExecutablePath.Equals($targetExe, [StringComparison]::OrdinalIgnoreCase) }
if ($runningTarget) { throw "Target EXE is running (PID $($runningTarget.ProcessId)); close it yourself before rebuilding. No process was stopped." }

New-Item -ItemType Directory -Force -Path $BuildDirectory, $OutputRoot, $LogDirectory | Out-Null
foreach ($checkedDirectory in @($BuildDirectory, $OutputRoot)) {
    $directoryItem = Get-Item -LiteralPath $checkedDirectory -Force
    if (($directoryItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Build/output directory is a symlink or junction and cannot be safely resolved: $checkedDirectory"
    }
}
$env:CYBERSPACE_OUTPUT = $OutputRoot
$timestamp = Get-Date -Format 'yyyyMMddTHHmmssfff'
$configureLog = Join-Path $LogDirectory "${featureKey}_configure_${timestamp}.log"
$buildLog = Join-Path $LogDirectory "${featureKey}_build_${timestamp}.log"
$enableValue = if ($FeatureSet -eq 'ON') { 'ON' } else { 'OFF' }
$configureArgs = @(
    '-S', $repoRoot,
    '-B', $BuildDirectory,
    '-G', 'Visual Studio 17 2022',
    '-A', 'x64',
    "-DENABLE_NEUROCONTROL=$enableValue",
    '-DUSE_SDL=OFF',
    '-DCEF_SUPPORT=ON',
    '-DXR_SUPPORT=ON',
    "-DOPENXR_SDK_DIR=$OpenXrSdk",
    "-DCEF_BINARY_DISTRIB_DIR=$CefDistribution",
    '-DINDIGO_SUPPORT=OFF',
    '-DBUGSPLAT_SUPPORT=OFF',
    "-DCYBERSPACE_OUTPUT_EXPECTED=$OutputRoot"
)

Write-Host "Feature configuration: NeuroControl $FeatureSet"
Write-Host "Source: $repoRoot"
Write-Host "Build tree: $BuildDirectory"
Write-Host "Output root: $OutputRoot"
Write-Host 'CMake regeneration guard: CYBERSPACE_OUTPUT must continue to equal the expected output root.'
& cmake @configureArgs 2>&1 | Tee-Object -FilePath $configureLog
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE; see $configureLog" }

$guiProject = Join-Path $BuildDirectory 'gui_client\gui_client.vcxproj'
if (-not (Test-Path -LiteralPath $guiProject -PathType Leaf)) { throw "Generated GUI project not found: $guiProject" }
$guiOutDirs = @()
$projects = Get-ChildItem -LiteralPath $BuildDirectory -Filter '*.vcxproj' -File -Recurse |
    Where-Object { $_.BaseName -notmatch '^CompilerId(C|CXX)$' }
if ($projects.Count -eq 0) { throw 'No generated Visual Studio projects were found.' }
foreach ($project in $projects) {
    [xml]$projectXml = Get-Content -LiteralPath $project.FullName -Raw
    $outDirs = @($projectXml.SelectNodes("//*[local-name()='OutDir']")) | ForEach-Object { $_.InnerText.Trim().TrimEnd('\', '/') }
    if ($outDirs.Count -eq 0) { continue }
    foreach ($outDir in $outDirs) {
        if (-not (Test-PathWithin $outDir $OutputRoot) -and -not (Test-PathWithin $outDir $BuildDirectory)) {
            throw "Generated project $($project.Name) writes outside the isolated build/output trees: '$outDir'"
        }
        if ($project.FullName.Equals($guiProject, [StringComparison]::OrdinalIgnoreCase)) {
            $guiOutDirs += $outDir
        }
    }
}
$outDirs = $guiOutDirs | Sort-Object -Unique
if ($outDirs.Count -eq 0) { throw 'No generated OutDir values were found in gui_client.vcxproj.' }
$expectedRelWithDebInfo = Join-Path $OutputRoot 'vs2022\cyberspace_x64\RelWithDebInfo'
if (-not ($outDirs | Where-Object { $_.Equals($expectedRelWithDebInfo, [StringComparison]::OrdinalIgnoreCase) })) {
    throw "RelWithDebInfo GUI OutDir does not match expected path '$expectedRelWithDebInfo'."
}

& cmake --build $BuildDirectory --config RelWithDebInfo --target gui_client --parallel 4 2>&1 | Tee-Object -FilePath $buildLog
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE; see $buildLog" }
if (-not (Test-Path -LiteralPath $targetExe -PathType Leaf)) { throw "Build reported success but target EXE is missing: $targetExe" }
$afterHash = (Get-FileHash -LiteralPath $activeExe -Algorithm SHA256).Hash
if ($afterHash -ne $activeExeExpectedSha256) { throw "Active client changed during isolated build: $afterHash" }
$targetHash = (Get-FileHash -LiteralPath $targetExe -Algorithm SHA256).Hash

Write-Host "Build succeeded. EXE: $targetExe"
Write-Host "EXE SHA-256: $targetHash"
Write-Host "Control EXE unchanged: $afterHash"
Write-Host "Configure log: $configureLog"
Write-Host "Build log: $buildLog"
