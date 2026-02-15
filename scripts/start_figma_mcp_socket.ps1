param(
  [string]$ProjectDir = "C:\\programming\\_tmp_talk_to_figma",
  [int]$Port = 3055
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $ProjectDir)) {
  throw "Missing directory: $ProjectDir"
}

if (-not (Get-Command bun -ErrorAction SilentlyContinue)) {
  throw "bun is not installed or not in PATH. Install bun first: https://bun.sh/"
}

Write-Host "Starting Talk To Figma MCP socket server..."
Write-Host "Dir:  $ProjectDir"
Write-Host "Port: $Port"
Write-Host ""
Write-Host "In Figma: open 'Talk To Figma MCP Plugin' and connect to port $Port."
Write-Host "Stop server with Ctrl+C."
Write-Host ""

Push-Location $ProjectDir
try {
  # src/socket.ts reads the port internally (default 3055). If you need a different port,
  # update the project or run with its supported env var/arg (if any).
  bun socket
}
finally {
  Pop-Location
}

