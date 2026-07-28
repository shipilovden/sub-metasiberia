param(
  [string]$BaseUrl = "https://vr.metasiberia.com",
  [string]$OutDir = "",
  [string]$Cookie = "",
  [string]$AdminUser = "",
  [string]$AdminPass = "",
  [string]$StorageState = "",
  [int]$Width = 1440,
  [int]$Height = 900,
  [switch]$NoFullPage
)

$ErrorActionPreference = "Stop"

function Assert-Command($name) {
  if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
    throw "Command '$name' not found in PATH."
  }
}

Assert-Command node
Assert-Command npm

$toolDir = "C:\programming\substrata\tools\site_capture"
if (-not (Test-Path $toolDir)) { throw "Missing $toolDir" }

Push-Location $toolDir
try {
  if (-not (Test-Path ".\node_modules")) {
    npm install | Out-Host
  }

  if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $OutDir = Join-Path $toolDir ("out_" + $stamp)
  }

  if (-not [string]::IsNullOrWhiteSpace($Cookie)) { $env:METASIBERIA_COOKIE = $Cookie }
  if (-not [string]::IsNullOrWhiteSpace($AdminUser)) { $env:METASIBERIA_ADMIN_USER = $AdminUser }
  if (-not [string]::IsNullOrWhiteSpace($AdminPass)) { $env:METASIBERIA_ADMIN_PASS = $AdminPass }
  if (-not [string]::IsNullOrWhiteSpace($StorageState)) { $env:METASIBERIA_STORAGE_STATE = $StorageState }

  $full = "true"
  if ($NoFullPage) { $full = "false" }

  $cmd = @(
    "node", "capture.mjs",
    "--base", $BaseUrl,
    "--out", $OutDir,
    "--width", $Width,
    "--height", $Height,
    "--fullPage", $full
  )
  & $cmd[0] $cmd[1..($cmd.Length - 1)] | Out-Host

  Write-Host ""
  Write-Host "Done. Output:"
  Write-Host $OutDir
  Write-Host ""
  Write-Host "Next: open Figma dev plugin 'Metasiberia Site Importer' and import:"
  Write-Host " - $OutDir\\manifest.json"
  Write-Host " - all PNG files from $OutDir"
}
finally {
  Pop-Location
}

