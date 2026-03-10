param(
    [string]$RepoSlug = "shipilovden/sub-metasiberia",
    [string]$SourceDir = "C:\programming\substrata\docs\wiki",
    [string]$WorkingDir = "C:\programming\_tmp_sub_metasiberia_wiki_publish",
    [string]$CommitMessage = "Sync docs/wiki from main repo"
)

$ErrorActionPreference = "Stop"

function Require-Command {
    param([string]$CommandName)

    if (-not (Get-Command $CommandName -ErrorAction SilentlyContinue)) {
        throw "Required command not found: $CommandName"
    }
}

Require-Command git

$resolvedSourceDir = (Resolve-Path $SourceDir).Path
$wikiRepoUrl = "https://github.com/$RepoSlug.wiki.git"

$publishItems = @(
    "Home.md",
    "01-Install-Windows.md",
    "02-Registration-and-Login.md",
    "03-First-Launch-and-Connection.md",
    "WIKI-PAGES-PLAN-RU.md",
    "_Sidebar.md",
    "_Footer.md",
    "images"
)

if (Test-Path $WorkingDir) {
    Remove-Item -Recurse -Force $WorkingDir
}

git clone $wikiRepoUrl $WorkingDir | Out-Host

Get-ChildItem -Path $WorkingDir -Force |
    Where-Object { $_.Name -ne ".git" } |
    Remove-Item -Recurse -Force

foreach ($item in $publishItems) {
    $sourcePath = Join-Path $resolvedSourceDir $item
    if (-not (Test-Path $sourcePath)) {
        throw "Missing source item: $sourcePath"
    }

    $targetPath = Join-Path $WorkingDir $item
    $targetParent = Split-Path -Parent $targetPath
    if (-not (Test-Path $targetParent)) {
        New-Item -ItemType Directory -Path $targetParent -Force | Out-Null
    }

    Copy-Item -Path $sourcePath -Destination $targetPath -Recurse -Force
}

$status = git -C $WorkingDir status --porcelain
if (-not $status) {
    Write-Output "No wiki changes to publish."
    exit 0
}

git -C $WorkingDir add -A | Out-Host
git -C $WorkingDir commit -m $CommitMessage | Out-Host
git -C $WorkingDir push origin master | Out-Host
