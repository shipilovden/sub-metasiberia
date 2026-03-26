param(
	[string]$Python = "python"
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$syncScript = Join-Path $scriptDir "sync_vive_recordings.py"

& $Python $syncScript --watch --latest-on-start 1 --poll-interval 5
