param(
    [string]$Server = "denshipilov@192.168.0.30",
    [string]$KeyPath = "$env:USERPROFILE\.ssh\metasiberia_backup_ed25519",
    [string]$DestinationRoot = "E:\MetasiberiaBackups",
    [int]$KeepStateArchives = 3,
    [int]$KeepServiceArchives = 14,
    [int64]$MaxStateArchiveGB = 35
)

$ErrorActionPreference = "Stop"

$ssh = "C:\Windows\System32\OpenSSH\ssh.exe"
$scp = "C:\Windows\System32\OpenSSH\scp.exe"
$commonSshArgs = @(
    "-i", $KeyPath,
    "-o", "BatchMode=yes",
    "-o", "StrictHostKeyChecking=accept-new",
    "-o", "ServerAliveInterval=30",
    "-o", "ServerAliveCountMax=6"
)

$stateDir = Join-Path $DestinationRoot "state"
$servicesDir = Join-Path $DestinationRoot "services"
$logsDir = Join-Path $DestinationRoot "logs"
New-Item -ItemType Directory -Force -Path $stateDir, $servicesDir, $logsDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$logPath = Join-Path $logsDir "backup_pull_$stamp.log"

function Write-Log {
    param([string]$Message)
    $line = "$(Get-Date -Format o) $Message"
    Write-Output $line
    Add-Content -Path $logPath -Value $line -Encoding UTF8
}

function Invoke-Remote {
    param([string]$Command)
    $output = & $ssh @commonSshArgs $Server $Command
    if ($LASTEXITCODE -ne 0) {
        throw "Remote command failed ($LASTEXITCODE): $Command"
    }
    return ($output -join "`n").Trim()
}

function Copy-RemoteFile {
    param(
        [string]$RemotePath,
        [string]$LocalDirectory,
        [int64]$MaxBytes = 0
    )

    if ([string]::IsNullOrWhiteSpace($RemotePath)) {
        return
    }

    $fileName = Split-Path $RemotePath -Leaf
    $destPath = Join-Path $LocalDirectory $fileName
    $remoteSize = [int64](Invoke-Remote "stat -c %s '$RemotePath'")

    if (($MaxBytes -gt 0) -and ($remoteSize -gt $MaxBytes)) {
        throw "Remote archive is too large for automatic pull: $RemotePath is $remoteSize bytes, limit is $MaxBytes bytes. Check backup contents on the server before downloading."
    }

    if ((Test-Path $destPath) -and ((Get-Item $destPath).Length -eq $remoteSize)) {
        Write-Log "Already present: $destPath ($remoteSize bytes)"
        return
    }

    $tmpPath = "$destPath.tmp"
    Remove-Item -LiteralPath $tmpPath -Force -ErrorAction SilentlyContinue
    Write-Log "Copying $RemotePath -> $destPath"
    & $scp @commonSshArgs "${Server}:$RemotePath" $tmpPath
    if ($LASTEXITCODE -ne 0) {
        Remove-Item -LiteralPath $tmpPath -Force -ErrorAction SilentlyContinue
        throw "scp failed for $RemotePath"
    }

    $localSize = (Get-Item $tmpPath).Length
    if ($localSize -ne $remoteSize) {
        Remove-Item -LiteralPath $tmpPath -Force -ErrorAction SilentlyContinue
        throw "Size mismatch for ${RemotePath}: remote=$remoteSize local=$localSize"
    }

    Move-Item -LiteralPath $tmpPath -Destination $destPath -Force
    Write-Log "Copied $destPath ($localSize bytes)"
}

function Keep-Newest {
    param(
        [string]$Directory,
        [string]$Pattern,
        [int]$Keep
    )
    Get-ChildItem -LiteralPath $Directory -Filter $Pattern -File |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -Skip $Keep |
        ForEach-Object {
            Write-Log "Removing old local archive: $($_.FullName)"
            Remove-Item -LiteralPath $_.FullName -Force
        }
}

Write-Log "Starting Metasiberia backup pull from $Server"

$latestStateArchive = Invoke-Remote "find /srv/metasiberia/data/backups -maxdepth 1 -type f -name 'metasiberia-server_cyberspace_server_state_*.tar.gz' -printf '%T@ %p\n' | sort -nr | head -1 | cut -d' ' -f2-"
$maxStateArchiveBytes = $MaxStateArchiveGB * 1024 * 1024 * 1024
Copy-RemoteFile -RemotePath $latestStateArchive -LocalDirectory $stateDir -MaxBytes $maxStateArchiveBytes

$serviceArchives = Invoke-Remote "find /srv/metasiberia/data/backups/services -maxdepth 1 -type f -name 'metasiberia-services_*.tar.gz' -printf '%T@ %p\n' 2>/dev/null | sort -nr | head -3 | cut -d' ' -f2- || true"
if (-not [string]::IsNullOrWhiteSpace($serviceArchives)) {
    foreach ($remotePath in ($serviceArchives -split "`n")) {
        Copy-RemoteFile -RemotePath $remotePath.Trim() -LocalDirectory $servicesDir
    }
}
else {
    Write-Log "No service archives found yet."
}

Keep-Newest -Directory $stateDir -Pattern "metasiberia-server_cyberspace_server_state_*.tar.gz" -Keep $KeepStateArchives
Keep-Newest -Directory $servicesDir -Pattern "metasiberia-services_*.tar.gz" -Keep $KeepServiceArchives

$manifestPath = Join-Path $DestinationRoot "latest-manifest.txt"
@(
    "Updated: $(Get-Date -Format o)",
    "Server: $Server",
    "State archive: $latestStateArchive",
    "Destination: $DestinationRoot"
) | Set-Content -Path $manifestPath -Encoding UTF8

Write-Log "Backup pull finished."
