param(
  [string]$SshHost = "metasiberia-v2",
  [string]$ServerPublicDir = "/root/cyberspace_server_state/webserver_public_files",
  [string]$ServerFragmentsDir = "/var/www/cyberspace/webserver_fragments",
  [string]$ServerWebClientDir = "/root/cyberspace_server_state/webclient",
  [switch]$SkipFragments,
  [switch]$SkipPublicFiles,
  [switch]$SkipWebClientHtml,
  [switch]$SkipWebClientButtons
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
$webclientHtmlSrc = Join-Path $repoRoot "webclient\webclient.html"
$buttonsSrc = Join-Path $repoRoot "resources\buttons"

if (-not (Test-Path $publicSrc)) { throw "Missing dir: $publicSrc" }
if (-not (Test-Path $fragsSrc))  { throw "Missing dir: $fragsSrc" }
if (-not (Test-Path $webclientHtmlSrc)) { throw "Missing file: $webclientHtmlSrc" }
if (-not (Test-Path $buttonsSrc)) { throw "Missing dir: $buttonsSrc" }

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"

function Deploy-Tar($localDirName, $localDirPath, $remoteDir, $watchPingDir) {
  if (-not $watchPingDir) { $watchPingDir = $remoteDir }
  Write-Host "Deploy: $localDirName -> ${SshHost}:$remoteDir"

  # PowerShell pipelines are not binary-safe, so we avoid `tar ... | ssh ...`.
  $safeLocalDirName = ($localDirName -replace '[\\/]', '_')
  $localTgz = Join-Path $env:TEMP ("${safeLocalDirName}_$timestamp.tgz")
  $remoteTgz = "/tmp/${safeLocalDirName}_$timestamp.tgz"

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
  $tmpDir = "/tmp/${safeLocalDirName}.__deploy_tmp__${timestamp}"
  $bakDir = "${remoteDir}.__bak__${timestamp}"

  $remoteCmd =
    "set -e; " +
    "mkdir -p $remoteDir; " +
    "rm -rf $tmpDir; mkdir -p $tmpDir; " +
    "tar -xzf $remoteTgz -C $tmpDir; rm -f $remoteTgz; " +
    "rm -rf $bakDir; cp -a $remoteDir $bakDir; " +
    "rsync -a --delete $tmpDir/$localDirName/ $remoteDir/; " +
    # Trigger the server's inotify watcher (it doesn't watch rename/move events by default).
    "echo ping > $watchPingDir/__reload_ping.txt; rm -f $watchPingDir/__reload_ping.txt; " +
    "rm -rf $tmpDir; " +
    "echo OK"

  & ssh $SshHost $remoteCmd | Out-Null
}

function Deploy-File($localFilePath, $remoteFilePath, $watchPingDir) {
  Write-Host "Deploy file: $localFilePath -> ${SshHost}:$remoteFilePath"

  $localName = [System.IO.Path]::GetFileName($localFilePath)
  $remoteTmp = "/tmp/${localName}_$timestamp"

  & scp $localFilePath "$SshHost`:$remoteTmp" | Out-Null

  $slashIndex = $remoteFilePath.LastIndexOf('/')
  if ($slashIndex -lt 1) { throw "Invalid remote file path: $remoteFilePath" }
  $remoteDir = $remoteFilePath.Substring(0, $slashIndex)
  $backupPath = "${remoteFilePath}.__bak__${timestamp}"

  $remoteCmd =
    "set -e; " +
    "mkdir -p $remoteDir; " +
    "if [ -f $remoteFilePath ]; then cp -a $remoteFilePath $backupPath; fi; " +
    "mv $remoteTmp $remoteFilePath; " +
    "echo ping > $watchPingDir/__reload_ping.txt; rm -f $watchPingDir/__reload_ping.txt; " +
    "echo OK"

  & ssh $SshHost $remoteCmd | Out-Null
}

if (-not $SkipPublicFiles) {
  Deploy-Tar -localDirName "webserver_public_files" -localDirPath $publicSrc -remoteDir $ServerPublicDir -watchPingDir $ServerPublicDir
}

if (-not $SkipFragments) {
  Deploy-Tar -localDirName "webserver_fragments" -localDirPath $fragsSrc -remoteDir $ServerFragmentsDir -watchPingDir $ServerFragmentsDir
}

if (-not $SkipWebClientHtml) {
  Deploy-File -localFilePath $webclientHtmlSrc -remoteFilePath "$ServerWebClientDir/webclient.html" -watchPingDir $ServerWebClientDir
}

if (-not $SkipWebClientButtons) {
  Deploy-Tar -localDirName "resources/buttons" -localDirPath $buttonsSrc -remoteDir "$ServerWebClientDir/data/resources/buttons" -watchPingDir $ServerWebClientDir
}

Write-Host "Done."
