param(
  [string]$SshHost = "metasiberia-v2",
  [string]$ServerPublicDir = "/root/cyberspace_server_state/webserver_public_files",
  [string]$ServerFragmentsDir = "/var/www/cyberspace/webserver_fragments",
  [switch]$SkipFragments,
  [switch]$SkipPublicFiles
)

$ErrorActionPreference = "Stop"

function Assert-Command($name) {
  if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
    throw "Command '$name' not found in PATH."
  }
}

Assert-Command ssh
Assert-Command tar

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$publicSrc = Join-Path $repoRoot "webserver_public_files"
$fragsSrc  = Join-Path $repoRoot "webserver_fragments"

if (-not (Test-Path $publicSrc)) { throw "Missing dir: $publicSrc" }
if (-not (Test-Path $fragsSrc))  { throw "Missing dir: $fragsSrc" }

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"

function Deploy-Tar($localDirName, $localDirPath, $remoteDir) {
  Write-Host "Deploy: $localDirName -> $SshHost:$remoteDir"

  # Extract to a temp directory then atomically replace. This avoids half-written trees if the connection drops.
  $remoteTmp = "$remoteDir.__deploy_tmp__$timestamp"
  $remoteBak = "$remoteDir.__bak__$timestamp"

  $remoteCmd = @"
set -e
mkdir -p '$remoteTmp'
tar -xzf - -C '$remoteTmp'
if [ -d '$remoteDir' ]; then
  mv '$remoteDir' '$remoteBak'
fi
mv '$remoteTmp/$localDirName' '$remoteDir'
echo 'OK'
"@

  Push-Location $repoRoot
  try {
    tar -czf - $localDirName | ssh $SshHost $remoteCmd
  }
  finally {
    Pop-Location
  }
}

if (-not $SkipPublicFiles) {
  Deploy-Tar -localDirName "webserver_public_files" -localDirPath $publicSrc -remoteDir $ServerPublicDir
}

if (-not $SkipFragments) {
  Deploy-Tar -localDirName "webserver_fragments" -localDirPath $fragsSrc -remoteDir $ServerFragmentsDir
}

Write-Host "Done."

