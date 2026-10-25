param(
    [Parameter(Mandatory = $true)][string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$reference = 'C:\programming\substrata_output_qt\vs2022\cyberspace_x64\RelWithDebInfo'
$output = [System.IO.Path]::GetFullPath($OutputDirectory)
$workspace = [System.IO.Path]::GetFullPath('C:\programming')
$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $output.StartsWith($workspace + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase) -or
    $output -notmatch '[\\/]substrata_nc_output_(off|on)_audit_20261024[\\/]vs2022[\\/]cyberspace_x64[\\/]RelWithDebInfo$' -or
    $output.Equals([System.IO.Path]::GetFullPath($reference), [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputDirectory must be one of the isolated audit build outputs; the active client is never a target.'
}
if (-not (Test-Path -LiteralPath (Join-Path $output 'gui_client.exe') -PathType Leaf)) {
    throw 'Build the isolated gui_client.exe before staging runtime assets.'
}

function CopyCheckedFile([string]$source, [string]$destination) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing runtime source: $source" }
    $expected = (Get-FileHash -Algorithm SHA256 -LiteralPath $source).Hash
    if (Test-Path -LiteralPath $destination) {
        if ((Get-FileHash -Algorithm SHA256 -LiteralPath $destination).Hash -ne $expected) {
            throw "Existing runtime file differs; refusing overwrite: $destination"
        }
    } else {
        New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination
        if ((Get-FileHash -Algorithm SHA256 -LiteralPath $destination).Hash -ne $expected) {
            throw "Runtime copy failed SHA-256 verification: $destination"
        }
    }
}

# The reference package uses the same Qt 5.15.16 and CEF distribution as the
# current CMake configuration. Copy only named runtime assets, never its GUI
# or server executable, local log, DLLs built by this integration, or settings.
$files = @(
    'Qt5Core.dll', 'Qt5Gamepad.dll', 'Qt5Gui.dll', 'Qt5Multimedia.dll',
    'Qt5MultimediaWidgets.dll', 'Qt5Network.dll', 'Qt5OpenGL.dll', 'Qt5Svg.dll', 'Qt5Widgets.dll',
    'chrome_elf.dll', 'd3dcompiler_47.dll', 'dxcompiler.dll', 'dxil.dll',
    'libcef.dll', 'libEGL.dll', 'libGLESv2.dll', 'openxr_loader.dll',
    'vk_swiftshader.dll', 'vulkan-1.dll',
    'chrome_100_percent.pak', 'chrome_200_percent.pak', 'resources.pak',
    'icudtl.dat', 'v8_context_snapshot.bin', 'vk_swiftshader_icd.json',
    'licence.txt', 'screenshot_bot.exe', 'screenshot_bot.pdb'
)
foreach ($name in $files) {
    CopyCheckedFile (Join-Path $reference $name) (Join-Path $output $name)
}

$directories = @('data', 'gamepads', 'imageformats', 'locales', 'mediaservice', 'platforms', 'resources', 'styles')
foreach ($directory in $directories) {
    $sourceRoot = Join-Path $reference $directory
    foreach ($file in Get-ChildItem -LiteralPath $sourceRoot -Recurse -File) {
        $relative = $file.FullName.Substring($reference.Length + 1)
        CopyCheckedFile $file.FullName (Join-Path $output $relative)
    }
}

# The repository has two resources absent from the older reference package.
$sourceResources = Join-Path $repository 'resources'
foreach ($file in Get-ChildItem -LiteralPath $sourceResources -Recurse -File) {
    $relative = $file.FullName.Substring($sourceResources.Length + 1)
    $destination = Join-Path (Join-Path $output 'data\resources') $relative
    CopyCheckedFile $file.FullName $destination
}

# Keep the ON package usable without the source checkout. Only source scripts,
# public knowledge files and the explicit local research manifest are copied;
# do not sweep ignored EEG directories or operator recordings.
if ($output -match '[\\/]substrata_nc_output_on_audit_20261024[\\/]') {
    $neuroRoot = Join-Path $repository 'neurocontrol'
    $runtimeFiles = @(
        Get-ChildItem -LiteralPath (Join-Path $neuroRoot 'tools') -Filter '*.py' -File
        Get-ChildItem -LiteralPath (Join-Path $neuroRoot 'processing') -Filter '*.py' -File
        Get-ChildItem -LiteralPath (Join-Path $neuroRoot 'docs\mcp') -Filter '*.md' -File
        Get-Item -LiteralPath (Join-Path $neuroRoot 'README.md')
        Get-Item -LiteralPath (Join-Path $neuroRoot 'requirements.txt')
        Get-Item -LiteralPath (Join-Path $neuroRoot 'requirements-lock.txt')
        Get-Item -LiteralPath (Join-Path $neuroRoot 'docs\LOCAL_DATA_MANIFEST.md')
    )
    foreach ($file in $runtimeFiles) {
        $relative = $file.FullName.Substring($neuroRoot.Length + 1)
        CopyCheckedFile $file.FullName (Join-Path (Join-Path $output 'neurocontrol') $relative)
    }
    $manifest = Join-Path $neuroRoot 'docs\LOCAL_DATA_MANIFEST.md'
    $manifestCount = 0
    foreach ($line in Get-Content -LiteralPath $manifest) {
        if ($line -notmatch '^\| `((?:models|datasets)/(?:raw|processed)/[^`]+|models/[^`]+)` \| `([0-9a-f]{64})` \|$') { continue }
        $relative = $Matches[1]
        $expected = $Matches[2].ToUpperInvariant()
        $source = [IO.Path]::GetFullPath((Join-Path $neuroRoot $relative))
        if (-not $source.StartsWith($neuroRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Manifest path escapes neurocontrol: $relative"
        }
        if ((Get-FileHash -Algorithm SHA256 -LiteralPath $source).Hash -ne $expected) {
            throw "Local research file differs from manifest: $relative"
        }
        CopyCheckedFile $source (Join-Path (Join-Path $output 'neurocontrol') $relative)
        ++$manifestCount
    }
    if ($manifestCount -ne 47) { throw "Expected 47 explicitly reviewed local research files; found $manifestCount" }
}

Write-Output "Runtime assets staged and SHA-256 checked in $output. The active executable was read only."
