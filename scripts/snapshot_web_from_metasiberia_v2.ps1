param(
  [string]$SshHost = "metasiberia-v2",
  [string]$OutDir = "",
  [string]$ServerPublicDir = "/root/cyberspace_server_state/webserver_public_files",
  [string]$ServerWebClientDir = "/root/cyberspace_server_state/webclient",
  [string]$ServerFragmentsDir = "/var/www/cyberspace/webserver_fragments"
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

if ([string]::IsNullOrWhiteSpace($OutDir)) {
  $repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
  $backupRoot = Join-Path $repoRoot "local_backups"
  New-Item -ItemType Directory -Force -Path $backupRoot | Out-Null

  $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
  $OutDir = Join-Path $backupRoot ("metasiberia_v2_web_snapshot_" + $timestamp)
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function Pull-RemoteDir($remoteDir, $localName) {
  $localPath = Join-Path $OutDir $localName
  New-Item -ItemType Directory -Force -Path $localPath | Out-Null

  Write-Host "Snapshot: ${SshHost}:$remoteDir -> $localPath"

  # PowerShell pipelines are not binary-safe, so we avoid `ssh ... | tar ...`.
  $remoteTgz = "/tmp/${localName}_$((Get-Date).ToString('yyyyMMdd_HHmmss')).tgz"
  $localTgz  = Join-Path $OutDir ($localName + ".tgz")

  $packCmd = "set -e; cd '$remoteDir'; tar -czf '$remoteTgz' ."
  & ssh $SshHost $packCmd | Out-Null

  & scp "$SshHost`:$remoteTgz" $localTgz | Out-Null
  & ssh $SshHost "rm -f '$remoteTgz'" | Out-Null

  tar -xzf $localTgz -C $localPath
  Remove-Item -Force $localTgz
}

Pull-RemoteDir -remoteDir $ServerPublicDir  -localName "webserver_public_files"
Pull-RemoteDir -remoteDir $ServerFragmentsDir -localName "webserver_fragments"
Pull-RemoteDir -remoteDir $ServerWebClientDir -localName "webclient"

Write-Host "Done. Snapshot saved to: $OutDir"
