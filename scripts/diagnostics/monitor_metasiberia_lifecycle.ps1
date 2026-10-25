[CmdletBinding()]
param(
	[ValidateRange(1, 50)]
	[int]$Cycles = 5,

	[ValidateRange(50, 5000)]
	[int]$PollIntervalMs = 100,

	[string]$ExecutablePath = 'C:\programming\substrata_output_qt\vs2022\cyberspace_x64\RelWithDebInfo\gui_client.exe',

	[string]$LogPath = '',

	[string]$OutputRoot = (Join-Path $env:LOCALAPPDATA 'Metasiberia\LifecycleDiagnostics'),

	[string]$DumpFolder = (Join-Path $env:LOCALAPPDATA 'Metasiberia\CrashDumps'),

	[ValidateRange(1, 256)]
	[int]$MinimumDumpFreeGiB = 12,

	[ValidateRange(5, 300)]
	[int]$WerPostExitWaitSeconds = 45,

	[switch]$AllowWithoutWerDumps
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ExecutablePath = [System.IO.Path]::GetFullPath($ExecutablePath)
if (-not (Test-Path -LiteralPath $ExecutablePath -PathType Leaf)) {
	throw "Metasiberia executable not found: $ExecutablePath"
}

function Resolve-ClientLogPath {
	param([string]$RequestedPath)

	if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
		return [System.IO.Path]::GetFullPath($RequestedPath)
	}

	$roaming = $env:APPDATA
	$metasiberia = Join-Path $roaming 'Metasiberia'
	$legacy = Join-Path $roaming 'Cyberspace'
	$hasMetasiberiaClientData =
		(Test-Path -LiteralPath (Join-Path $metasiberia 'log.txt')) -or
		(Test-Path -LiteralPath (Join-Path $metasiberia 'settings_store.xml')) -or
		(Test-Path -LiteralPath (Join-Path $metasiberia 'screenshots') -PathType Container) -or
		(Test-Path -LiteralPath (Join-Path $metasiberia 'indigo_scenes') -PathType Container)

	if ($hasMetasiberiaClientData) {
		return Join-Path $metasiberia 'log.txt'
	}
	if (Test-Path -LiteralPath $legacy -PathType Container) {
		return Join-Path $legacy 'log.txt'
	}
	return Join-Path $metasiberia 'log.txt'
}

function Write-AtomicText {
	param([string]$Path, [string]$Text)

	$tempPath = "$Path.tmp"
	[System.IO.File]::WriteAllText($tempPath, $Text, [System.Text.UTF8Encoding]::new($false))
	Move-Item -LiteralPath $tempPath -Destination $Path -Force
}

function ConvertTo-CsvField {
	param([AllowNull()][object]$Value)

	if ($null -eq $Value) {
		$text = ''
	} elseif ($Value -is [DateTime]) {
		$text = $Value.ToUniversalTime().ToString('o')
	} else {
		$text = [Convert]::ToString($Value, [Globalization.CultureInfo]::InvariantCulture)
	}
	return '"' + $text.Replace('"', '""') + '"'
}

function Write-CsvRow {
	param([System.IO.StreamWriter]$Writer, [object[]]$Values)

	$fields = foreach ($value in $Values) { ConvertTo-CsvField $value }
	$Writer.WriteLine(($fields -join ','))
	$Writer.Flush()
}

function Write-SessionManifest {
	param([string]$Path, [DateTime]$StartedUtc, [int]$TargetCycles, [string]$ExePath, [string]$OutputDirectory, [array]$RunSummaries, [string]$Status)

	$manifest = [ordered]@{
		schema = 'metasiberia-lifecycle-monitor-v2'
		status = $Status
		monitor_started_utc = $StartedUtc.ToString('o')
		monitor_finished_utc = if ($Status -in @('completed', 'interrupted')) { [DateTime]::UtcNow.ToString('o') } else { $null }
		target_cycles = $TargetCycles
		executable_path = $ExePath
		output_directory = $OutputDirectory
		wer_preflight = $script:WerPreflight
		runs = @($RunSummaries)
	}
	Write-AtomicText -Path $Path -Text ($manifest | ConvertTo-Json -Depth 8)
}

function Write-CycleManifest {
	param([object]$State)

	$manifest = [ordered]@{
		schema = 'metasiberia-lifecycle-cycle-v2'
		status = $State.Status
		cycle = $State.Cycle
		pid = $State.Pid
		executable_path = $State.ExecutablePath
		executable_sha256 = $State.ExecutableSha256
		executable_file_version = $State.ExecutableFileVersion
		process_creation_utc = $State.ProcessCreationUtc
		process_detected_utc = $State.ProcessDetectedUtc
		main_window_first_seen_utc = $State.MainWindowFirstSeenUtc
		close_preparation_log_observed_utc = $State.ClosePreparationObservedUtc
		main_window_disappeared_observed_utc = $State.MainWindowDisappearedUtc
		main_window_disappeared_run_elapsed_seconds = $State.MainWindowDisappearedElapsedSeconds
		process_exit_observed_utc = $State.ProcessExitObservedUtc
		process_exit_code = $State.ExitCode
		process_exit_code_error = $State.ExitCodeError
		child_process_observations_path = $State.ChildProcessesPath
		child_process_capture = 'sampled direct children named codex.exe and browser_process.exe; executable path and creation time recorded, command line omitted; first-seen/first-absent observation is not an exact start/exit time'
		log_path = $State.LogPath
		log_snapshot_path = $State.LogSnapshotPath
		log_capture_status = $State.LogCaptureStatus
		log_baseline_sha256 = $State.LogBaselineSha256
		log_baseline_last_write_utc = $State.LogBaselineLastWriteUtc
		log_last_source_write_utc = $State.LogLastSourceWriteUtc
		wer_dump_files = @($State.WerDumpFiles)
		wer_dump_status = $State.WerDumpStatus
		wer_dump_wait_deadline_utc = $State.WerDumpWaitDeadlineUtc
		wer_dump_last_fingerprint = $State.WerDumpLastFingerprint
		wer_dump_stable_since_utc = $State.WerDumpStableSinceUtc
		monitor_poll_interval_ms = $PollIntervalMs
		resource_sample_interval_ms = 1000
		window_disappearance_detection = 'first monitor poll where MainWindowHandle changed from non-zero to zero; observation time, not exact OS close time'
		process_exit_detection = 'first monitor poll where the matching process is absent from Win32_Process'
	}
	Write-AtomicText -Path $State.ManifestPath -Text ($manifest | ConvertTo-Json -Depth 6)
}

function Get-DiskFreeBytes {
	param([string]$Path)

	$root = [System.IO.Path]::GetPathRoot([System.IO.Path]::GetFullPath($Path))
	$drive = [System.IO.DriveInfo]::new($root)
	return [long]$drive.AvailableFreeSpace
}

function Get-DumpInventory {
	param([string]$Folder)

	if (-not (Test-Path -LiteralPath $Folder -PathType Container)) { return @() }
	return @(Get-ChildItem -LiteralPath $Folder -File -ErrorAction SilentlyContinue |
		Where-Object { $_.Extension -in @('.dmp', '.mdmp') } |
		ForEach-Object {
			[pscustomobject]@{
				full_name = $_.FullName
				length_bytes = [long]$_.Length
				last_write_utc = $_.LastWriteTimeUtc.ToString('o')
			}
		})
}

function Get-NewDumpInventory {
	param([object]$State)

	$before = @{}
	foreach ($item in @($State.DumpInventoryAtStart)) {
		$before[$item.full_name.ToLowerInvariant()] = '{0}|{1}' -f $item.length_bytes, $item.last_write_utc
	}
	$current = Get-DumpInventory -Folder $DumpFolder
	return @($current | Where-Object {
		$key = $_.full_name.ToLowerInvariant()
		$fingerprint = '{0}|{1}' -f $_.length_bytes, $_.last_write_utc
		(-not $before.ContainsKey($key)) -or ($before[$key] -ne $fingerprint)
	})
}

function Get-DumpsSinceBaseline {
	param([string]$Folder, [array]$Baseline)

	$before = @{}
	foreach ($item in @($Baseline)) {
		$before[$item.full_name.ToLowerInvariant()] = '{0}|{1}' -f $item.length_bytes, $item.last_write_utc
	}
	$current = Get-DumpInventory -Folder $Folder
	return @($current | Where-Object {
		$key = $_.full_name.ToLowerInvariant()
		$fingerprint = '{0}|{1}' -f $_.length_bytes, $_.last_write_utc
		(-not $before.ContainsKey($key)) -or ($before[$key] -ne $fingerprint)
	})
}

function Write-SessionDumpInventory {
	param([string]$Path, [string]$StartedUtc, [array]$Baseline, [array]$NewDumps)

	$inventory = [ordered]@{
		schema = 'metasiberia-session-wer-dumps-v1'
		monitor_started_utc = $StartedUtc
		updated_utc = [DateTime]::UtcNow.ToString('o')
		baseline_files = @($Baseline)
		new_or_changed_files = @($NewDumps)
		content_read = $false
	}
	Write-AtomicText -Path $Path -Text ($inventory | ConvertTo-Json -Depth 5)
}

function Get-LogTextSnapshot {
	param([string]$Path)

	if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
		return $null
	}
	$info = Get-Item -LiteralPath $Path
	$stream = [System.IO.File]::Open(
		$Path,
		[System.IO.FileMode]::Open,
		[System.IO.FileAccess]::Read,
		([System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete)
	)
	try {
		$reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::UTF8, $true, 4096, $true)
		try { $text = $reader.ReadToEnd() } finally { $reader.Dispose() }
	} finally { $stream.Dispose() }
	$sha = [System.Security.Cryptography.SHA256]::HashData([System.Text.Encoding]::UTF8.GetBytes($text))
	return [pscustomobject]@{
		text = $text
		length = [long]$info.Length
		last_write_utc = $info.LastWriteTimeUtc
		sha256 = [System.BitConverter]::ToString($sha).Replace('-', '')
	}
}

function Write-RunEvent {
	param([object]$State, [string]$Event, [string]$Details = '')

	Write-CsvRow -Writer $State.EventWriter -Values @(
		[DateTime]::UtcNow.ToString('o'),
		[math]::Round($State.Stopwatch.Elapsed.TotalSeconds, 3),
		$Event,
		$State.Cycle,
		$State.Pid,
		$Details
	)
}

function Write-ChildProcessRow {
	param([object]$State, [string]$Event, [object]$ProcessInfo = $null, [string]$Details = '')

	$pidValue = if ($ProcessInfo) { [int]$ProcessInfo.ProcessId } else { '' }
	$parentPid = if ($ProcessInfo) { [int]$ProcessInfo.ParentProcessId } else { $State.Pid }
	$name = if ($ProcessInfo) { [string]$ProcessInfo.Name } else { '' }
	$createdUtc = ''
	$path = if ($ProcessInfo) { [string]$ProcessInfo.ExecutablePath } else { '' }
	if ($ProcessInfo -and $ProcessInfo.CreationDate) {
		try { $createdUtc = [DateTime]::Parse([string]$ProcessInfo.CreationDate).ToUniversalTime().ToString('o') } catch {}
	}
	Write-CsvRow -Writer $State.ChildProcessWriter -Values @(
		[DateTime]::UtcNow.ToString('o'),
		[math]::Round($State.Stopwatch.Elapsed.TotalSeconds, 3),
		$Event,
		$pidValue,
		$parentPid,
		$name,
		$createdUtc,
		$path,
		$Details
	)
}

function Update-ChildProcessSnapshot {
	param([object]$State)

	if ($State.ChildProcessTimer.Elapsed.TotalMilliseconds -lt 500) { return }
	$State.ChildProcessTimer.Restart()
	try {
		$children = @(Get-CimInstance -ClassName Win32_Process -Filter "ParentProcessId=$($State.Pid)" -ErrorAction Stop |
			Where-Object { $_.Name -in @('codex.exe', 'browser_process.exe') })
		$current = @{}
		foreach ($child in $children) {
			$created = [DateTime]::Parse([string]$child.CreationDate).ToUniversalTime()
			$key = '{0}:{1}' -f [int]$child.ProcessId, $created.Ticks
			$current[$key] = $true
			if (-not $State.ObservedChildProcesses.ContainsKey($key)) {
				$State.ObservedChildProcesses[$key] = [pscustomobject]@{
					ProcessInfo = $child
					FirstAbsentWritten = $false
				}
				Write-ChildProcessRow -State $State -Event 'FIRST_SEEN' -ProcessInfo $child
			}
		}
		foreach ($key in @($State.ObservedChildProcesses.Keys)) {
			$record = $State.ObservedChildProcesses[$key]
			if (-not $current.ContainsKey($key) -and -not $record.FirstAbsentWritten) {
				$record.FirstAbsentWritten = $true
				Write-ChildProcessRow -State $State -Event 'FIRST_ABSENT' -ProcessInfo $record.ProcessInfo
			}
		}
		$State.LastChildProcessError = ''
	} catch {
		$message = $_.Exception.Message
		if ($State.LastChildProcessError -ne $message) {
			$State.LastChildProcessError = $message
			Write-ChildProcessRow -State $State -Event 'SAMPLE_ERROR' -Details $message
			Write-RunEvent -State $State -Event 'CHILD_PROCESS_SAMPLE_ERROR' -Details $message
		}
	}
}

function Update-LogDiagnostics {
	param([object]$State, [string]$RunText)

	if ($null -eq $RunText) {
		return
	}
	$lines = @($RunText -split "`r?`n")
	$previousLines = @($State.LastProcessedRunLines)
	$commonPrefix = 0
	$prefixLimit = [math]::Min($previousLines.Length, $lines.Length)
	while ($commonPrefix -lt $prefixLimit -and $previousLines[$commonPrefix] -ceq $lines[$commonPrefix]) {
		$commonPrefix++
	}

	$diagnosticPattern = '(?i)Lifecycle:|(^|\s)(error|fatal|exception|failed|crash|timeout|assert)\b|c0000005|access violation'
	for ($index = $commonPrefix; $index -lt $lines.Length; ++$index) {
		$line = $lines[$index]
		if ($line -match '(?i)Lifecycle:\s*close preparation started') {
			if ([string]::IsNullOrEmpty($State.ClosePreparationObservedUtc) -and
				$State.LogCaptureStatus -eq 'current_run_lifecycle_marker_confirmed') {
				$State.ClosePreparationObservedUtc = [DateTime]::UtcNow.ToString('o')
			}
		}
		if ($line -match $diagnosticPattern) {
			$kind = if ($line -match '(?i)Lifecycle:') { 'Lifecycle' } else { 'ErrorOrWarning' }
			Write-CsvRow -Writer $State.LifecycleWriter -Values @(
				[DateTime]::UtcNow.ToString('o'),
				[math]::Round($State.Stopwatch.Elapsed.TotalSeconds, 3),
				($index + 1),
				$kind,
				$line
			)
		}
	}
	$State.LastProcessedRunLines = $lines
	$State.LifecycleWriter.Flush()
	$filtered = @($lines | Where-Object { $_ -match $diagnosticPattern })
	[System.IO.File]::WriteAllLines(
		$State.LifecycleErrorsPath,
		[string[]]$filtered,
		[System.Text.UTF8Encoding]::new($false)
	)
}

function Save-LogSnapshot {
	param([object]$State, [switch]$Force)

	if (-not (Test-Path -LiteralPath $State.LogPath -PathType Leaf)) {
		return
	}
	try {
		$source = Get-LogTextSnapshot -Path $State.LogPath
		if (-not $source) { return }
		if (-not $Force -and
			$source.length -eq $State.LastLogLength -and
			$source.last_write_utc.Ticks -eq $State.LastLogWriteTicks) {
			return
		}

		$State.LastLogLength = $source.length
		$State.LastLogWriteTicks = $source.last_write_utc.Ticks
		$State.LogLastSourceWriteUtc = $source.last_write_utc.ToString('o')
		if ($source.last_write_utc -lt $State.ProcessCreationDateTimeUtc -or
			$source.sha256 -eq $State.LogBaselineSha256) {
			$State.LogCaptureStatus = 'waiting_for_current_process_log'
			Write-CycleManifest -State $State
			return
		}

		$startupMarker = '(?im)^.*Lifecycle:\s*window construction started.*$'
		$matches = [regex]::Matches($source.text, $startupMarker)
		$baselinePrefixMatches = $State.LogBaselineText.Length -gt 0 -and
			$source.text.StartsWith($State.LogBaselineText, [StringComparison]::Ordinal)
		$runText = $null
		if ($matches.Count -gt 0) {
			if ($baselinePrefixMatches -and $source.text.Length -gt $State.LogBaselineText.Length) {
				$runText = $source.text.Substring($State.LogBaselineText.Length)
				if ($runText -match $startupMarker) {
					$State.LogCaptureStatus = 'current_run_lifecycle_marker_confirmed'
				} else {
					$State.LogCaptureStatus = 'fresh_log_delta_waiting_for_lifecycle_marker'
				}
			} else {
				$runText = $source.text.Substring($matches[$matches.Count - 1].Index)
				$State.LogCaptureStatus = 'current_run_lifecycle_marker_confirmed'
			}
		} elseif ($baselinePrefixMatches) {
			$runText = $source.text.Substring($State.LogBaselineText.Length)
			$State.LogCaptureStatus = 'fresh_log_delta_waiting_for_lifecycle_marker'
		} else {
			$State.LogCaptureStatus = 'unassociated_rewrite_without_lifecycle_marker'
			$State.LastLogCopyError = ''
			Write-CycleManifest -State $State
			return
		}

		$tempPath = "$($State.LogSnapshotPath).tmp"
		[System.IO.File]::WriteAllText($tempPath, $runText, [System.Text.UTF8Encoding]::new($false))
		Move-Item -LiteralPath $tempPath -Destination $State.LogSnapshotPath -Force
		$State.LastLogCopyError = ''
		Update-LogDiagnostics -State $State -RunText $runText
		Write-CycleManifest -State $State
	} catch {
		$message = $_.Exception.Message
		if ($message -ne $State.LastLogCopyError) {
			Write-RunEvent -State $State -Event 'LOG_SNAPSHOT_ERROR' -Details $message
			$State.LastLogCopyError = $message
		}
	}
}

function Get-WerLocalDumpsSettings {
	$relativeKey = 'SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\gui_client.exe'
	$baseKey = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
		[Microsoft.Win32.RegistryHive]::LocalMachine,
		[Microsoft.Win32.RegistryView]::Registry64
	)
	try {
		$key = $baseKey.OpenSubKey($relativeKey, $false)
		$values = @{}
		if ($key) {
			try {
				foreach ($name in $key.GetValueNames()) {
					$values[$name] = [pscustomobject]@{
						value = $key.GetValue($name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
						kind = [string]$key.GetValueKind($name)
					}
				}
			} finally { $key.Dispose() }
		}
		$debugKey = $baseKey.OpenSubKey('SOFTWARE\Microsoft\Windows NT\CurrentVersion\AeDebug', $false)
		$debugger = ''
		$auto = ''
		if ($debugKey) {
			try {
				$debugger = [string]$debugKey.GetValue('Debugger', '')
				$auto = [string]$debugKey.GetValue('Auto', '')
			} finally { $debugKey.Dispose() }
		}
		return [pscustomobject]@{
			key_path = 'HKLM\' + $relativeKey
			key_exists = [bool]$key
			values = $values
			ae_debug_debugger = $debugger
			ae_debug_auto = $auto
		}
	} finally { $baseKey.Dispose() }
}

function Get-WerPreflight {
	param([string]$Folder, [int]$RequiredFreeGiB)

	$settings = Get-WerLocalDumpsSettings
	$freeBytes = Get-DiskFreeBytes -Path $Folder
	$requiredBytes = [long]$RequiredFreeGiB * 1GB
	$configuredFolder = ''
	if ($settings.values.ContainsKey('DumpFolder')) {
		$configuredFolder = [Environment]::ExpandEnvironmentVariables([string]$settings.values['DumpFolder'].value)
	}
	$folderMatches = $configuredFolder -and [System.IO.Path]::GetFullPath($configuredFolder).Equals(
		[System.IO.Path]::GetFullPath($Folder), [StringComparison]::OrdinalIgnoreCase)
	$countMatches = $settings.values.ContainsKey('DumpCount') -and
		$settings.values['DumpCount'].kind -eq 'DWord' -and [int]$settings.values['DumpCount'].value -eq 3
	$typeMatches = $settings.values.ContainsKey('DumpType') -and
		$settings.values['DumpType'].kind -eq 'DWord' -and [int]$settings.values['DumpType'].value -eq 2
	$folderKindMatches = $settings.values.ContainsKey('DumpFolder') -and
		$settings.values['DumpFolder'].kind -eq 'ExpandString'
	$autoDebuggerEnabled = $settings.ae_debug_auto -eq '1'
	$freeSpaceOk = $freeBytes -ge $requiredBytes
	$folderExists = Test-Path -LiteralPath $Folder -PathType Container
	$ready = $settings.key_exists -and $folderMatches -and $countMatches -and $typeMatches -and
		$folderKindMatches -and $freeSpaceOk -and $folderExists -and -not $autoDebuggerEnabled
	return [pscustomobject]@{
		ready = [bool]$ready
		registry_key = $settings.key_path
		key_exists = $settings.key_exists
		dump_folder_registry_value = if ($settings.values.ContainsKey('DumpFolder')) { [string]$settings.values['DumpFolder'].value } else { '' }
		dump_folder_resolved = $configuredFolder
		dump_folder_kind = if ($settings.values.ContainsKey('DumpFolder')) { $settings.values['DumpFolder'].kind } else { '' }
		dump_folder_exists = [bool]$folderExists
		dump_count = if ($settings.values.ContainsKey('DumpCount')) { $settings.values['DumpCount'].value } else { $null }
		dump_count_kind = if ($settings.values.ContainsKey('DumpCount')) { $settings.values['DumpCount'].kind } else { '' }
		dump_type = if ($settings.values.ContainsKey('DumpType')) { $settings.values['DumpType'].value } else { $null }
		dump_type_kind = if ($settings.values.ContainsKey('DumpType')) { $settings.values['DumpType'].kind } else { '' }
		free_disk_bytes = $freeBytes
		free_disk_gib = [math]::Round($freeBytes / 1GB, 2)
		minimum_free_gib = $RequiredFreeGiB
		ae_debug_auto = $settings.ae_debug_auto
		ae_debugger_registered = -not [string]::IsNullOrWhiteSpace($settings.ae_debug_debugger)
		ae_debugger_command = $settings.ae_debug_debugger
	}
}

function Start-RunState {
	param([object]$ProcessInfo, [int]$CycleNumber, [string]$SessionDirectory, [string]$ExePath, [string]$ExeHash, [string]$FileVersion)

	$pidValue = [int]$ProcessInfo.ProcessId
	$creationUtc = [DateTime]::Parse([string]$ProcessInfo.CreationDate).ToUniversalTime()
	$runDirectory = Join-Path $SessionDirectory ('cycle-{0:D2}-pid-{1}' -f $CycleNumber, $pidValue)
	New-Item -ItemType Directory -Path $runDirectory -Force | Out-Null

	$eventsPath = Join-Path $runDirectory 'events.csv'
	$resourcesPath = Join-Path $runDirectory 'resource-samples.csv'
	$lifecyclePath = Join-Path $runDirectory 'lifecycle-events.csv'
	$childProcessesPath = Join-Path $runDirectory 'child-processes.csv'
	$eventWriter = [System.IO.StreamWriter]::new($eventsPath, $false, [System.Text.UTF8Encoding]::new($true))
	$resourceWriter = [System.IO.StreamWriter]::new($resourcesPath, $false, [System.Text.UTF8Encoding]::new($true))
	$lifecycleWriter = [System.IO.StreamWriter]::new($lifecyclePath, $false, [System.Text.UTF8Encoding]::new($true))
	$childProcessWriter = [System.IO.StreamWriter]::new($childProcessesPath, $false, [System.Text.UTF8Encoding]::new($true))
	Write-CsvRow -Writer $eventWriter -Values @('timestamp_utc','session_elapsed_seconds','event','cycle','pid','details')
	Write-CsvRow -Writer $resourceWriter -Values @('timestamp_utc','run_elapsed_seconds','pid','thread_count','handle_count','working_set_bytes','private_bytes','virtual_bytes','main_window_handle','free_disk_bytes','wer_dump_count','wer_dump_total_bytes')
	Write-CsvRow -Writer $lifecycleWriter -Values @('observed_utc','run_elapsed_seconds','log_line','kind','message')
	Write-CsvRow -Writer $childProcessWriter -Values @('observed_utc','run_elapsed_seconds','event','pid','parent_pid','process_name','process_creation_utc','executable_path','details')
	$resolvedLogPath = Resolve-ClientLogPath -RequestedPath $LogPath
	$baseline = Get-LogTextSnapshot -Path $resolvedLogPath
	$dumpBaseline = Get-DumpInventory -Folder $DumpFolder

	$state = [pscustomobject]@{
		Cycle = $CycleNumber
		Pid = $pidValue
		Key = ('{0}:{1}' -f $pidValue, $creationUtc.Ticks)
		Status = 'running'
		ExecutablePath = $ExePath
		ExecutableSha256 = $ExeHash
		ExecutableFileVersion = $FileVersion
		ProcessCreationUtc = $creationUtc.ToString('o')
		ProcessDetectedUtc = [DateTime]::UtcNow.ToString('o')
		MainWindowFirstSeenUtc = ''
		ClosePreparationObservedUtc = ''
		MainWindowDisappearedUtc = ''
		MainWindowDisappearedElapsedSeconds = $null
		ProcessExitObservedUtc = ''
		ExitCode = $null
		ExitCodeError = ''
		LogPath = $resolvedLogPath
		LogSnapshotPath = Join-Path $runDirectory 'client-log.txt'
		LifecycleErrorsPath = Join-Path $runDirectory 'lifecycle-and-errors.txt'
		ManifestPath = Join-Path $runDirectory 'cycle.json'
		Directory = $runDirectory
		EventWriter = $eventWriter
		ResourceWriter = $resourceWriter
		LifecycleWriter = $lifecycleWriter
		ChildProcessesPath = $childProcessesPath
		ChildProcessWriter = $childProcessWriter
		ChildProcessTimer = [Diagnostics.Stopwatch]::StartNew()
		ObservedChildProcesses = @{}
		LastChildProcessError = ''
		ProcessHandle = $null
		Stopwatch = [Diagnostics.Stopwatch]::StartNew()
		ResourceTimer = [Diagnostics.Stopwatch]::StartNew()
		WindowlessWarningWritten = $false
		PostWindowWarningWritten = $false
		LastMainWindowHandle = [Int64]0
		EverSawMainWindow = $false
		LogBaselineText = if ($baseline) { $baseline.text } else { '' }
		LogBaselineSha256 = if ($baseline) { $baseline.sha256 } else { '' }
		LogBaselineLastWriteUtc = if ($baseline) { $baseline.last_write_utc.ToString('o') } else { '' }
		LogLastSourceWriteUtc = ''
		LogCaptureStatus = 'waiting_for_current_process_log'
		LastLogLength = if ($baseline) { $baseline.length } else { -1L }
		LastLogWriteTicks = if ($baseline) { $baseline.last_write_utc.Ticks } else { -1L }
		LastProcessedRunLines = @()
		LastLogCopyError = ''
		DumpInventoryAtStart = $dumpBaseline
		WerDumpFiles = @()
		WerDumpStatus = 'not_checked'
		WerDumpWaitDeadlineUtc = ''
		WerDumpLastFingerprint = ''
		WerDumpStableSinceUtc = ''
		DiskWarningWritten = $false
		LastProcessEnumerationError = ''
		ProcessCreationDateTimeUtc = $creationUtc
	}
	try { $state.ProcessHandle = [Diagnostics.Process]::GetProcessById($pidValue) } catch {}

	Write-RunEvent -State $state -Event 'PROCESS_START_DETECTED' -Details ('creation_utc=' + $state.ProcessCreationUtc)
	Write-CycleManifest -State $state
	Write-Host ("Cycle {0}/{1}: PID {2}; fresh journal -> {3}" -f $CycleNumber, $Cycles, $pidValue, $state.LogSnapshotPath) -ForegroundColor Cyan
	return $state
}

function Save-WindowsApplicationEvents {
	param([string]$SessionDirectory, [DateTime]$SessionStartedUtc, [array]$RunSummaries)

	$outputPath = Join-Path $SessionDirectory 'windows-application-events.txt'
	try {
		$events = Get-WinEvent -FilterHashtable @{
			LogName = 'Application'
			Id = @(1000, 1001, 1002)
			StartTime = $SessionStartedUtc.ToLocalTime()
		} -MaxEvents 500 -ErrorAction Stop
		$runPidValues = [System.Collections.Generic.List[string]]::new()
		foreach ($run in $RunSummaries) {
			if ($null -eq $run) { continue }
			$pidValue = $null
			if ($run -is [System.Collections.IDictionary]) {
				foreach ($key in $run.Keys) {
					if ([string]$key -ieq 'pid') {
						$pidValue = $run[$key]
						break
					}
				}
			} else {
				$pidProperty = $run.PSObject.Properties | Where-Object { $_.Name -ieq 'pid' } | Select-Object -First 1
				if ($null -ne $pidProperty) { $pidValue = $pidProperty.Value }
			}
			if ([string]$pidValue -match '^\d+$') { $runPidValues.Add([string]$pidValue) }
		}
		$runPids = @($runPidValues | Sort-Object -Unique)
		$runPidPattern = $null
		if ($runPids.Count -gt 0) {
			$escapedPids = @($runPids | ForEach-Object { [regex]::Escape($_) })
			$runPidPattern = '(?<!\d)(' + ($escapedPids -join '|') + ')(?!\d)'
		}
		$relevant = @($events | Where-Object {
			$matchesClient = $_.Message -match '(?i)gui_client\.exe'
			$matchesCodex = $_.Message -match '(?i)\bcodex\.exe\b'
			$matchesRunPid = $runPidPattern -and $_.Message -match $runPidPattern
			$matchesClient -or $matchesCodex -or $matchesRunPid
		})
		$lines = foreach ($event in $relevant) {
			'[{0:o}] Provider={1}; EventId={2}; RecordId={3}' -f $event.TimeCreated.ToUniversalTime(), $event.ProviderName, $event.Id, $event.RecordId
			$event.Message
			''
		}
		if (@($lines).Count -eq 0) {
			$lines = @('No matching Application Error, Windows Error Reporting, or Application Hang events were found during this monitoring session.')
		}
		[System.IO.File]::WriteAllLines($outputPath, [string[]]$lines, [System.Text.UTF8Encoding]::new($false))
	} catch {
		[System.IO.File]::WriteAllText(
			$outputPath,
			'Application event log query was unavailable: ' + $_.Exception.Message,
			[System.Text.UTF8Encoding]::new($false)
		)
	}
}

$ExecutablePath = [System.IO.Path]::GetFullPath($ExecutablePath)
$DumpFolder = [System.IO.Path]::GetFullPath($DumpFolder)
$script:WerPreflight = Get-WerPreflight -Folder $DumpFolder -RequiredFreeGiB $MinimumDumpFreeGiB
if (-not $script:WerPreflight.ready -and -not $AllowWithoutWerDumps) {
	$summary = $script:WerPreflight | ConvertTo-Json -Depth 5 -Compress
	throw "WER LocalDumps is not ready for gui_client.exe or disk space is below threshold. No monitoring session was started. Preflight: $summary"
}

$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$sessionStartedUtc = [DateTime]::UtcNow
$sessionName = $sessionStartedUtc.ToString('yyyyMMddTHHmmssfffZ')
$sessionDirectory = Join-Path $OutputRoot $sessionName
New-Item -ItemType Directory -Path $sessionDirectory -Force | Out-Null
$sessionManifestPath = Join-Path $sessionDirectory 'session.json'
$sessionDumpInventoryPath = Join-Path $sessionDirectory 'wer-dumps.json'
$sessionDumpBaseline = @(Get-DumpInventory -Folder $DumpFolder)
$sessionNewDumps = @(Get-DumpsSinceBaseline -Folder $DumpFolder -Baseline $sessionDumpBaseline)
$sessionDumpFingerprint = ($sessionNewDumps | ForEach-Object { '{0}|{1}|{2}' -f $_.full_name, $_.length_bytes, $_.last_write_utc }) -join "`n"
Write-SessionDumpInventory -Path $sessionDumpInventoryPath -StartedUtc $sessionStartedUtc.ToString('o') -Baseline $sessionDumpBaseline -NewDumps $sessionNewDumps
$sessionSummaries = [System.Collections.Generic.List[object]]::new()
$activeRuns = @{}
$knownProcessKeys = @{}
$nextCycle = 0
$status = 'monitoring'
$exeHash = (Get-FileHash -LiteralPath $ExecutablePath -Algorithm SHA256).Hash
$exeVersion = (Get-Item -LiteralPath $ExecutablePath).VersionInfo.FileVersion
Write-SessionManifest -Path $sessionManifestPath -StartedUtc $sessionStartedUtc -TargetCycles $Cycles -ExePath $ExecutablePath -OutputDirectory $sessionDirectory -RunSummaries @() -Status $status

Write-Host "Monitoring only; this script will not launch or stop Metasiberia." -ForegroundColor Green
Write-Host "Executable: $ExecutablePath"
Write-Host ("WER LocalDumps ready: {0}; dump folder: {1}; free disk: {2} GiB (minimum {3} GiB)." -f $script:WerPreflight.ready, $DumpFolder, $script:WerPreflight.free_disk_gib, $MinimumDumpFreeGiB)
Write-Host "Waiting for $Cycles sequential launch/close cycles. Ctrl+C stops monitoring without touching the client."
Write-Host "Output: $sessionDirectory"

try {
	while (($nextCycle -lt $Cycles) -or ($activeRuns.Count -gt 0)) {
		$matchingProcesses = @()
		$processEnumerationSucceeded = $false
		try {
			$matchingProcesses = @(Get-CimInstance -ClassName Win32_Process -Filter "Name='gui_client.exe'" -ErrorAction Stop | Where-Object {
				$_.ExecutablePath -and
				[System.IO.Path]::GetFullPath($_.ExecutablePath).Equals($ExecutablePath, [StringComparison]::OrdinalIgnoreCase)
			})
			$processEnumerationSucceeded = $true
		} catch {
			Write-Warning ("Could not enumerate gui_client.exe: " + $_.Exception.Message)
			$processEnumerationError = $_.Exception.Message
		}

		foreach ($processInfo in $matchingProcesses) {
			$created = [DateTime]::Parse([string]$processInfo.CreationDate).ToUniversalTime()
			$key = '{0}:{1}' -f [int]$processInfo.ProcessId, $created.Ticks
			if (-not $knownProcessKeys.ContainsKey($key) -and $nextCycle -lt $Cycles) {
				$nextCycle++
				$knownProcessKeys[$key] = $true
				$state = Start-RunState -ProcessInfo $processInfo -CycleNumber $nextCycle -SessionDirectory $sessionDirectory -ExePath $ExecutablePath -ExeHash $exeHash -FileVersion $exeVersion
				$activeRuns[$key] = $state
				$sessionSummaries.Add([ordered]@{
					cycle = $nextCycle
					pid = [int]$processInfo.ProcessId
					process_creation_utc = $created.ToString('o')
					directory = $state.Directory
					log_capture_status = $state.LogCaptureStatus
					wer_dump_status = $state.WerDumpStatus
					wer_dump_files = @()
				})
				Write-SessionManifest -Path $sessionManifestPath -StartedUtc $sessionStartedUtc -TargetCycles $Cycles -ExePath $ExecutablePath -OutputDirectory $sessionDirectory -RunSummaries $sessionSummaries.ToArray() -Status $status
			}
		}

		$processIdsPresent = @{}
		foreach ($processInfo in $matchingProcesses) {
			$processIdsPresent[[int]$processInfo.ProcessId] = $true
		}

		if ($processEnumerationSucceeded) {
			$updatedSessionNewDumps = @(Get-DumpsSinceBaseline -Folder $DumpFolder -Baseline $sessionDumpBaseline)
			$updatedSessionDumpFingerprint = ($updatedSessionNewDumps | ForEach-Object { '{0}|{1}|{2}' -f $_.full_name, $_.length_bytes, $_.last_write_utc }) -join "`n"
			if ($updatedSessionDumpFingerprint -cne $sessionDumpFingerprint) {
				$sessionNewDumps = $updatedSessionNewDumps
				$sessionDumpFingerprint = $updatedSessionDumpFingerprint
				Write-SessionDumpInventory -Path $sessionDumpInventoryPath -StartedUtc $sessionStartedUtc.ToString('o') -Baseline $sessionDumpBaseline -NewDumps $sessionNewDumps
				if ($sessionNewDumps.Count -gt 0) {
					Write-Host ("New or updated WER dump file detected: {0}" -f $sessionNewDumps[-1].full_name) -ForegroundColor Yellow
				}
			}
		}

		foreach ($key in @($activeRuns.Keys)) {
			$state = $activeRuns[$key]
			Update-ChildProcessSnapshot -State $state
			if (-not $processEnumerationSucceeded) {
				if ($state.LastProcessEnumerationError -ne $processEnumerationError) {
					$state.LastProcessEnumerationError = $processEnumerationError
					Write-RunEvent -State $state -Event 'PROCESS_ENUMERATION_ERROR' -Details $processEnumerationError
				}
				continue
			}
			$state.LastProcessEnumerationError = ''
			$isPresent = $processIdsPresent.ContainsKey($state.Pid)
			if (-not $isPresent) {
				if ([string]::IsNullOrEmpty($state.ProcessExitObservedUtc)) {
					Save-LogSnapshot -State $state -Force
					$state.Status = 'exited_waiting_for_wer_dump'
					$state.ProcessExitObservedUtc = [DateTime]::UtcNow.ToString('o')
					$state.WerDumpWaitDeadlineUtc = [DateTime]::UtcNow.AddSeconds($WerPostExitWaitSeconds).ToString('o')
					if ($state.ProcessHandle) {
						try {
							$state.ProcessHandle.Refresh()
							if ($state.ProcessHandle.HasExited) {
								$state.ExitCode = $state.ProcessHandle.ExitCode
							}
						} catch {
							$state.ExitCodeError = $_.Exception.Message
						}
					}
					Write-RunEvent -State $state -Event 'PROCESS_EXIT_OBSERVED' -Details ('exit_code=' + [string]$state.ExitCode)
					Write-RunEvent -State $state -Event 'WER_DUMP_COLLECTION_WAIT_STARTED' -Details ('wait_seconds=' + $WerPostExitWaitSeconds)
					Write-CycleManifest -State $state
				}

				$state.WerDumpFiles = @(Get-NewDumpInventory -State $state)
				$dumpWaitExpired = [DateTime]::UtcNow -ge [DateTime]::Parse($state.WerDumpWaitDeadlineUtc).ToUniversalTime()
				$dumpFingerprint = if ($state.WerDumpFiles.Count -gt 0) {
					($state.WerDumpFiles | ForEach-Object { '{0}|{1}|{2}' -f $_.full_name, $_.length_bytes, $_.last_write_utc }) -join "`n"
				} else { '' }
				if ($dumpFingerprint -and $dumpFingerprint -ceq $state.WerDumpLastFingerprint) {
					if ([string]::IsNullOrEmpty($state.WerDumpStableSinceUtc)) {
						$state.WerDumpStableSinceUtc = [DateTime]::UtcNow.ToString('o')
					}
				} else {
					$state.WerDumpLastFingerprint = $dumpFingerprint
					$state.WerDumpStableSinceUtc = if ($dumpFingerprint) { [DateTime]::UtcNow.ToString('o') } else { '' }
				}
				$dumpStable = $dumpFingerprint -and
					-not [string]::IsNullOrEmpty($state.WerDumpStableSinceUtc) -and
					([DateTime]::UtcNow - [DateTime]::Parse($state.WerDumpStableSinceUtc).ToUniversalTime()).TotalSeconds -ge 2
				if (-not $dumpStable -and -not $dumpWaitExpired) {
					$state.WerDumpStatus = 'waiting_for_wer_dump_or_timeout'
					Write-CycleManifest -State $state
					continue
				}

				$state.WerDumpStatus = if ($dumpStable) { 'new_dump_detected_stable' } elseif ($dumpFingerprint) { 'dump_detected_but_not_stable_at_timeout' } else { 'no_new_dump_after_wait' }
				$state.Status = 'exited'
				Write-RunEvent -State $state -Event 'WER_DUMP_COLLECTION_WAIT_FINISHED' -Details ('status=' + $state.WerDumpStatus + '; dump_count=' + $state.WerDumpFiles.Count)
				Write-CycleManifest -State $state
				$state.EventWriter.Dispose()
				$state.ResourceWriter.Dispose()
				$state.LifecycleWriter.Dispose()
				$state.ChildProcessWriter.Dispose()
				if ($state.ProcessHandle) { $state.ProcessHandle.Dispose() }
				$sessionSummaries | Where-Object { $_.cycle -eq $state.Cycle } | ForEach-Object {
					$_.status = $state.Status
					$_.process_exit_observed_utc = $state.ProcessExitObservedUtc
					$_.process_exit_code = $state.ExitCode
					$_.main_window_disappeared_observed_utc = $state.MainWindowDisappearedUtc
					$_.close_preparation_log_observed_utc = $state.ClosePreparationObservedUtc
					$_.wer_dump_status = $state.WerDumpStatus
					$_.wer_dump_files = @($state.WerDumpFiles)
				}
				Save-WindowsApplicationEvents -SessionDirectory $sessionDirectory -SessionStartedUtc $sessionStartedUtc -RunSummaries $sessionSummaries.ToArray()
				Write-SessionManifest -Path $sessionManifestPath -StartedUtc $sessionStartedUtc -TargetCycles $Cycles -ExePath $ExecutablePath -OutputDirectory $sessionDirectory -RunSummaries $sessionSummaries.ToArray() -Status $status
				Write-Host ("Cycle {0}/{1}: PID {2} exited; exit code {3}; process exit observed at {4}." -f $state.Cycle, $Cycles, $state.Pid, [string]$state.ExitCode, $state.ProcessExitObservedUtc) -ForegroundColor Green
				$activeRuns.Remove($key)
				continue
			}

			Save-LogSnapshot -State $state
			try {
				$process = Get-Process -Id $state.Pid -ErrorAction Stop
				$windowHandle = [Int64]$process.MainWindowHandle
				if ($windowHandle -ne 0) {
					$state.LastMainWindowHandle = $windowHandle
					if (-not $state.EverSawMainWindow) {
						$state.EverSawMainWindow = $true
						$state.MainWindowFirstSeenUtc = [DateTime]::UtcNow.ToString('o')
						Write-RunEvent -State $state -Event 'MAIN_WINDOW_FIRST_SEEN' -Details ('hwnd=' + $windowHandle)
						Write-CycleManifest -State $state
					}
				} elseif ($state.EverSawMainWindow -and [string]::IsNullOrEmpty($state.MainWindowDisappearedUtc)) {
					$state.MainWindowDisappearedUtc = [DateTime]::UtcNow.ToString('o')
					$state.MainWindowDisappearedElapsedSeconds = [math]::Round($state.Stopwatch.Elapsed.TotalSeconds, 3)
					Write-RunEvent -State $state -Event 'MAIN_WINDOW_DISAPPEARED' -Details ('previous_hwnd=' + $state.LastMainWindowHandle)
					Write-CycleManifest -State $state
					Write-Warning ("PID {0}: main window disappeared but the process is still alive. Capture a dump before ending it." -f $state.Pid)
				}

				if ($state.ResourceTimer.Elapsed.TotalMilliseconds -ge 1000) {
					$state.ResourceTimer.Restart()
					$freeDiskBytes = Get-DiskFreeBytes -Path $DumpFolder
					$dumpInventory = Get-DumpInventory -Folder $DumpFolder
					$dumpBytes = [long](($dumpInventory | Measure-Object -Property length_bytes -Sum).Sum)
					Write-CsvRow -Writer $state.ResourceWriter -Values @(
						[DateTime]::UtcNow.ToString('o'),
						[math]::Round($state.Stopwatch.Elapsed.TotalSeconds, 3),
						$state.Pid,
						$process.Threads.Count,
						$process.HandleCount,
						$process.WorkingSet64,
						$process.PrivateMemorySize64,
						$process.VirtualMemorySize64,
						$windowHandle,
						$freeDiskBytes,
						$dumpInventory.Count,
						$dumpBytes
					)
					if ($freeDiskBytes -lt ([long]$MinimumDumpFreeGiB * 1GB) -and -not $state.DiskWarningWritten) {
						$state.DiskWarningWritten = $true
						Write-RunEvent -State $state -Event 'DUMP_DISK_FREE_SPACE_BELOW_THRESHOLD' -Details ('free_bytes=' + $freeDiskBytes + '; minimum_gib=' + $MinimumDumpFreeGiB)
						Write-Warning ("PID {0}: free space for WER full dumps fell below {1} GiB; avoid another test until space is checked." -f $state.Pid, $MinimumDumpFreeGiB)
					}
				}

				$elapsedSeconds = $state.Stopwatch.Elapsed.TotalSeconds
				if (-not $state.EverSawMainWindow -and $elapsedSeconds -ge 90 -and -not $state.WindowlessWarningWritten) {
					$state.WindowlessWarningWritten = $true
					Write-RunEvent -State $state -Event 'STARTUP_NO_WINDOW_THRESHOLD' -Details 'Process remained alive without a visible main-window handle for 90 seconds; this is a diagnostic warning, not proof of a hang.'
					Write-Warning ("PID {0}: no main window has appeared after 90 seconds; check its Lifecycle log and consider capturing a dump." -f $state.Pid)
				}
				if ($state.EverSawMainWindow -and $null -ne $state.MainWindowDisappearedElapsedSeconds -and
					($elapsedSeconds - $state.MainWindowDisappearedElapsedSeconds) -ge 15 -and -not $state.PostWindowWarningWritten) {
					$state.PostWindowWarningWritten = $true
					Write-RunEvent -State $state -Event 'POST_WINDOW_PROCESS_STILL_RUNNING_THRESHOLD' -Details 'Process remained alive for at least 15 seconds after the main window disappeared.'
					Write-Warning ("PID {0}: still alive after the main window disappeared. In Task Manager > Details, right-click gui_client.exe > Create dump file; save the shown path before ending the process." -f $state.Pid)
				}
			} catch {
				Write-RunEvent -State $state -Event 'PROCESS_SAMPLE_ERROR' -Details $_.Exception.Message
			}
		}

		Start-Sleep -Milliseconds $PollIntervalMs
	}
	$status = 'completed'
} finally {
	foreach ($state in @($activeRuns.Values)) {
		try { Save-LogSnapshot -State $state -Force } catch {}
		try { Write-CycleManifest -State $state } catch {}
		try { $state.EventWriter.Flush(); $state.ResourceWriter.Flush(); $state.LifecycleWriter.Flush(); $state.ChildProcessWriter.Flush() } catch {}
	}
	Save-WindowsApplicationEvents -SessionDirectory $sessionDirectory -SessionStartedUtc $sessionStartedUtc -RunSummaries $sessionSummaries.ToArray()
	$sessionNewDumps = @(Get-DumpsSinceBaseline -Folder $DumpFolder -Baseline $sessionDumpBaseline)
	Write-SessionDumpInventory -Path $sessionDumpInventoryPath -StartedUtc $sessionStartedUtc.ToString('o') -Baseline $sessionDumpBaseline -NewDumps $sessionNewDumps
	if ($status -ne 'completed') { $status = 'interrupted' }
	Write-SessionManifest -Path $sessionManifestPath -StartedUtc $sessionStartedUtc -TargetCycles $Cycles -ExePath $ExecutablePath -OutputDirectory $sessionDirectory -RunSummaries $sessionSummaries.ToArray() -Status $status
	foreach ($state in @($activeRuns.Values)) {
		try { $state.EventWriter.Dispose(); $state.ResourceWriter.Dispose(); $state.LifecycleWriter.Dispose(); $state.ChildProcessWriter.Dispose() } catch {}
		if ($state.ProcessHandle) { $state.ProcessHandle.Dispose() }
	}
}

Write-Host "Monitoring $status. Diagnostic files are in: $sessionDirectory" -ForegroundColor Green
