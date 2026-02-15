param(
  [string]$BaseUrl = "https://vr.metasiberia.com",
  [string]$OutDir = "",
  [string]$Cookie = "",
  [int]$MaxTimeSec = 25
)

$ErrorActionPreference = "Stop"

function Assert-Command($name) {
  if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
    throw "Command '$name' not found in PATH."
  }
}

Assert-Command curl.exe

if ([string]::IsNullOrWhiteSpace($OutDir)) {
  $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
  $OutDir = Join-Path $env:TEMP ("metasiberia_site_snapshot_" + $stamp)
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$headers = @()
if (-not [string]::IsNullOrWhiteSpace($Cookie)) {
  # Do not store cookies in repo; pass via param only.
  $headers += @("-H", ("Cookie: " + $Cookie))
}

$pages = @(
  @{ name = "root";   path = "/" },
  @{ name = "map";    path = "/map" },
  @{ name = "news";   path = "/news" },
  @{ name = "photos"; path = "/photos" },
  @{ name = "signup"; path = "/signup" },
  @{ name = "login";  path = "/login" },
  # Requires admin session cookie:
  @{ name = "account";      path = "/account" },
  @{ name = "admin";        path = "/admin" },
  @{ name = "admin_users";  path = "/admin_users" }
)

$assets = @(
  @{ name = "main.css";      path = "/files/main.css" },
  @{ name = "root-page.js";  path = "/files/root-page.js" },
  @{ name = "favicon.png";   path = "/files/favicon.png" },
  @{ name = "favicon.ico";   path = "/files/favicon.ico" }
)

function Fetch-One($url, $outPath) {
  $dir = Split-Path -Parent $outPath
  if (-not [string]::IsNullOrWhiteSpace($dir)) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
  }

  $code = & curl.exe -sS -L --max-time $MaxTimeSec @headers -o $outPath -w "%{http_code}" $url
  return [int]$code
}

$results = @()

foreach ($p in $pages) {
  $url = ($BaseUrl.TrimEnd("/") + $p.path)
  $outFile = Join-Path $OutDir ($p.name + ".html")
  Write-Host ("GET " + $url)
  $code = Fetch-One -url $url -outPath $outFile
  $results += [pscustomobject]@{ kind="page"; name=$p.name; path=$p.path; url=$url; status=$code; file=$outFile }
}

foreach ($a in $assets) {
  $url = ($BaseUrl.TrimEnd("/") + $a.path)
  $outFile = Join-Path $OutDir ("assets\\" + $a.name)
  Write-Host ("GET " + $url)
  $code = Fetch-One -url $url -outPath $outFile
  $results += [pscustomobject]@{ kind="asset"; name=$a.name; path=$a.path; url=$url; status=$code; file=$outFile }
}

$meta = [pscustomobject]@{
  base_url = $BaseUrl
  out_dir = $OutDir
  created_utc = (Get-Date).ToUniversalTime().ToString("o")
  note = "Admin/account pages require Cookie param (not stored)."
  items = $results
}

$metaPath = Join-Path $OutDir "snapshot.json"
$meta | ConvertTo-Json -Depth 6 | Out-File -Encoding utf8 $metaPath

Write-Host ("Wrote " + $metaPath)
Write-Host ("OutDir: " + $OutDir)

