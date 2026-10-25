[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
	[switch]$Apply,

	[string]$DumpFolder = (Join-Path $env:LOCALAPPDATA 'Metasiberia\CrashDumps'),

	[ValidateRange(1, 256)]
	[int]$MinimumFreeGiB = 12
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$defaultDumpFolder = [System.IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Metasiberia\CrashDumps'))
$DumpFolder = [System.IO.Path]::GetFullPath($DumpFolder)
$RegistryRelativePath = 'SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\gui_client.exe'
$RegistryDisplayPath = 'HKLM\' + $RegistryRelativePath
$DumpFolderRegistryValue = if ($DumpFolder.Equals($defaultDumpFolder, [StringComparison]::OrdinalIgnoreCase)) {
	'%LOCALAPPDATA%\Metasiberia\CrashDumps'
} else {
	$DumpFolder
}
$TargetValues = [ordered]@{
	DumpFolder = [pscustomobject]@{ Value = $DumpFolderRegistryValue; Kind = [Microsoft.Win32.RegistryValueKind]::ExpandString }
	DumpCount = [pscustomobject]@{ Value = 3; Kind = [Microsoft.Win32.RegistryValueKind]::DWord }
	DumpType = [pscustomobject]@{ Value = 2; Kind = [Microsoft.Win32.RegistryValueKind]::DWord }
}

function Get-RegistrySnapshot {
	$base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
		[Microsoft.Win32.RegistryHive]::LocalMachine,
		[Microsoft.Win32.RegistryView]::Registry64
	)
	try {
		$key = $base.OpenSubKey($RegistryRelativePath, $false)
		$values = @{}
		if ($key) {
			try {
				foreach ($name in $key.GetValueNames()) {
					$values[$name] = [pscustomobject]@{
						Value = $key.GetValue($name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
						Kind = $key.GetValueKind($name)
					}
				}
			} finally { $key.Dispose() }
		}
		$aeDebug = $base.OpenSubKey('SOFTWARE\Microsoft\Windows NT\CurrentVersion\AeDebug', $false)
		$debugger = ''
		$auto = ''
		if ($aeDebug) {
			try {
				$debugger = [string]$aeDebug.GetValue('Debugger', '')
				$auto = [string]$aeDebug.GetValue('Auto', '')
			} finally { $aeDebug.Dispose() }
		}
		return [pscustomobject]@{
			KeyExists = [bool]$key
			Values = $values
			AeDebugDebugger = $debugger
			AeDebugAuto = $auto
		}
	} finally { $base.Dispose() }
}

function Test-IsAdministrator {
	$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
	$principal = [Security.Principal.WindowsPrincipal]::new($identity)
	return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-AvailableBytes {
	$root = [System.IO.Path]::GetPathRoot($DumpFolder)
	return [long][System.IO.DriveInfo]::new($root).AvailableFreeSpace
}

function Test-Conflicts {
	param([hashtable]$ExistingValues)

	$conflicts = [System.Collections.Generic.List[string]]::new()
	foreach ($name in $TargetValues.Keys) {
		if (-not $ExistingValues.ContainsKey($name)) { continue }
		$existing = $ExistingValues[$name]
		$target = $TargetValues[$name]
		if ($existing.Kind -ne $target.Kind -or [string]$existing.Value -cne [string]$target.Value) {
			$conflicts.Add(("{0}: existing {1} ({2}); requested {3} ({4})" -f $name, $existing.Value, $existing.Kind, $target.Value, $target.Kind))
		}
	}
	return $conflicts.ToArray()
}

$snapshot = Get-RegistrySnapshot
$availableBytes = Get-AvailableBytes
$availableGiB = [math]::Round($availableBytes / 1GB, 2)
$requiredBytes = [long]$MinimumFreeGiB * 1GB
$conflicts = @(Test-Conflicts -ExistingValues $snapshot.Values)

Write-Output 'WER LocalDumps plan for this application only:'
Write-Output ("  Registry key: {0}" -f $RegistryDisplayPath)
Write-Output ("  DumpFolder [REG_EXPAND_SZ]: {0}" -f $DumpFolderRegistryValue)
Write-Output '  DumpCount [REG_DWORD]: 3'
Write-Output '  DumpType [REG_DWORD]: 2 (full user-mode dump)'
Write-Output ("  Resolved folder: {0}" -f $DumpFolder)
Write-Output ("  Free space: {0} GiB; required before collection: at least {1} GiB" -f $availableGiB, $MinimumFreeGiB)
Write-Output ("  Existing gui_client.exe key: {0}" -f $snapshot.KeyExists)
Write-Output ("  Existing AeDebug Debugger: {0}" -f $(if ($snapshot.AeDebugDebugger) { $snapshot.AeDebugDebugger } else { '<not set>' }))
Write-Output ("  Existing AeDebug Auto: {0}" -f $(if ($snapshot.AeDebugAuto) { $snapshot.AeDebugAuto } else { '<not set; automatic mode is not enabled by this value>' }))
Write-Output '  AeDebug values and all other LocalDumps settings will remain unchanged.'

if ($conflicts.Count -gt 0) {
	throw ('Refusing to overwrite existing per-application values: ' + ($conflicts -join '; '))
}
if ($snapshot.AeDebugAuto -eq '1') {
	throw 'Automatic AeDebug is enabled. Microsoft documents that WER LocalDumps does not collect a dump in this mode. The debugger settings will not be changed.'
}
if ($availableBytes -lt $requiredBytes) {
	throw ("Free space is below the configured threshold ({0} GiB). No changes were made." -f $MinimumFreeGiB)
}

if (-not $Apply) {
	Write-Output 'Read-only preflight complete. No folder or registry values were changed. Add -Apply only after explicit approval.'
	return
}

if (-not (Test-IsAdministrator)) {
	throw 'Applying per-application WER LocalDumps requires an elevated administrator PowerShell session. No changes were made.'
}

$missingValues = @($TargetValues.Keys | Where-Object { -not $snapshot.Values.ContainsKey($_) })
if ($missingValues.Count -eq 0 -and $snapshot.KeyExists -and (Test-Path -LiteralPath $DumpFolder -PathType Container)) {
	Write-Output 'The requested per-application WER values and folder already match. No changes were needed.'
	return
}

$action = 'Create the dump folder and set only DumpFolder, DumpCount, and DumpType under the gui_client.exe LocalDumps key.'
if ($PSCmdlet.ShouldProcess($RegistryDisplayPath, $action)) {
	New-Item -ItemType Directory -Path $DumpFolder -Force | Out-Null
	$base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
		[Microsoft.Win32.RegistryHive]::LocalMachine,
		[Microsoft.Win32.RegistryView]::Registry64
	)
	try {
		$key = $base.CreateSubKey($RegistryRelativePath, $true)
		if (-not $key) { throw "Could not create $RegistryDisplayPath" }
		try {
			foreach ($name in $TargetValues.Keys) {
				$target = $TargetValues[$name]
				$key.SetValue($name, $target.Value, $target.Kind)
			}
			$key.Flush()
		} finally { $key.Dispose() }
	} finally { $base.Dispose() }

	$verified = Get-RegistrySnapshot
	$verificationConflicts = @(Test-Conflicts -ExistingValues $verified.Values)
	if (-not $verified.KeyExists -or $verificationConflicts.Count -gt 0 -or
		-not (Test-Path -LiteralPath $DumpFolder -PathType Container)) {
		throw ('WER LocalDumps post-write verification failed: ' + ($verificationConflicts -join '; '))
	}
	Write-Output 'Per-application WER LocalDumps settings were written and read back successfully.'
	Write-Output ("Dump folder ACL inherits from: {0}" -f (Split-Path -Parent $DumpFolder))
	Write-Output 'No application was launched, no test crash was generated, and no other registry keys were changed.'
}
