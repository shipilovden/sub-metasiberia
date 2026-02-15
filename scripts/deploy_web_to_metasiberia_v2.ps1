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
Assert-Command scp

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$publicSrc = Join-Path $repoRoot "webserver_public_files"
$fragsSrc  = Join-Path $repoRoot "webserver_fragments"

if (-not (Test-Path $publicSrc)) { throw "Missing dir: $publicSrc" }
if (-not (Test-Path $fragsSrc))  { throw "Missing dir: $fragsSrc" }

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"

function Deploy-Tar($localDirName, $localDirPath, $remoteDir) {
  Write-Host "Deploy: $localDirName -> ${SshHost}:$remoteDir"

  # PowerShell pipelines are not binary-safe, so we avoid `tar ... | ssh ...`.
  $localTgz = Join-Path $env:TEMP ("${localDirName}_$timestamp.tgz")
  $remoteTgz = "/tmp/${localDirName}_$timestamp.tgz"

  Push-Location $repoRoot
  try {
    tar -czf $localTgz $localDirName
  }
  finally {
    Pop-Location
  }

  & scp $localTgz "$SshHost`:$remoteTgz" | Out-Null
  Remove-Item -Force $localTgz

  # IMPORTANT (Linux): the server uses inotify watches on these directories.
  # Replacing the directory (mv/rename) breaks the watch descriptor (it watches the old inode),
  # so we must update contents IN PLACE. We still create a backup copy.
  # NOTE: pass a single-line command to ssh to avoid argument splitting/newline issues.
  $tmpDir = "/tmp/${localDirName}.__deploy_tmp__${timestamp}"
  $bakDir = "${remoteDir}.__bak__${timestamp}"

  $remoteCmd =
    "set -e; " +
    "mkdir -p $remoteDir; " +
    "rm -rf $tmpDir; mkdir -p $tmpDir; " +
    "tar -xzf $remoteTgz -C $tmpDir; rm -f $remoteTgz; " +
    "rm -rf $bakDir; cp -a $remoteDir $bakDir; " +
    "rsync -a --delete $tmpDir/$localDirName/ $remoteDir/; " +
    # Trigger the server's inotify watcher (it doesn't watch rename/move events by default).
    "echo ping > $remoteDir/__reload_ping.txt; rm -f $remoteDir/__reload_ping.txt; " +
    "rm -rf $tmpDir; " +
    "echo OK"

  & ssh $SshHost $remoteCmd | Out-Null
}

if (-not $SkipPublicFiles) {
  Deploy-Tar -localDirName "webserver_public_files" -localDirPath $publicSrc -remoteDir $ServerPublicDir
}

if (-not $SkipFragments) {
  Deploy-Tar -localDirName "webserver_fragments" -localDirPath $fragsSrc -remoteDir $ServerFragmentsDir
}

Write-Host "Done."
