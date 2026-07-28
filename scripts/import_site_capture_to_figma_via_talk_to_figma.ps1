param(
  [string]$Channel = "0xfhnf3f",
  [string]$InDir = "C:\\programming\\substrata\\tools\\site_capture\\out_20260215_224933",
  [string]$WsUrl = "ws://localhost:3055"
)

$ErrorActionPreference = "Stop"

function Read-Json($path) {
  return Get-Content $path -Raw | ConvertFrom-Json
}

function Send-WsText($ws, [string]$text) {
  $bytes = [System.Text.Encoding]::UTF8.GetBytes($text)
  $seg = [ArraySegment[byte]]::new($bytes)
  $null = $ws.SendAsync($seg, [System.Net.WebSockets.WebSocketMessageType]::Text, $true, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
}

function Recv-WsText($ws, [int]$timeoutMs = 30000) {
  $buffer = New-Object byte[] 1048576
  $seg = [ArraySegment[byte]]::new($buffer)
  $cts = New-Object System.Threading.CancellationTokenSource
  $cts.CancelAfter($timeoutMs)
  try {
    $res = $ws.ReceiveAsync($seg, $cts.Token).GetAwaiter().GetResult()
  } catch {
    throw "WebSocket receive timeout after ${timeoutMs}ms"
  }
  if ($res.MessageType -eq [System.Net.WebSockets.WebSocketMessageType]::Close) {
    throw "WebSocket closed"
  }
  return [System.Text.Encoding]::UTF8.GetString($buffer, 0, $res.Count)
}

function Wait-ForResponse($ws, [string]$id, [int]$timeoutMs = 60000) {
  $deadline = (Get-Date).AddMilliseconds($timeoutMs)
  while ((Get-Date) -lt $deadline) {
    $msgText = Recv-WsText -ws $ws -timeoutMs ([Math]::Min(15000, [int]($deadline - (Get-Date)).TotalMilliseconds))
    $obj = $msgText | ConvertFrom-Json

    # We care about broadcast messages that contain message.id == id.
    if ($obj.type -eq "broadcast" -and $obj.message -and $obj.message.id -eq $id) {
      # Ignore echoed commands.
      if ($obj.message.command) { continue }
      if ($obj.message.error) { throw "Figma command failed ($id): $($obj.message.error)" }
      return $obj.message.result
    }
  }
  throw "Timeout waiting response for id=$id"
}

function Send-Command($ws, [string]$channel, [string]$command, $params, [int]$timeoutMs = 60000) {
  $id = [Guid]::NewGuid().ToString()
  $payload = @{
    id      = $id
    type    = "message"
    channel = $channel
    message = @{
      id      = $id
      command = $command
      params  = $params
    }
  } | ConvertTo-Json -Depth 8

  Send-WsText -ws $ws -text $payload
  return (Wait-ForResponse -ws $ws -id $id -timeoutMs $timeoutMs)
}

if (-not (Test-Path $InDir)) { throw "Missing InDir: $InDir" }
$manifestPath = Join-Path $InDir "manifest.json"
if (-not (Test-Path $manifestPath)) { throw "Missing manifest.json: $manifestPath" }

$manifest = Read-Json $manifestPath
if (-not $manifest.items) { throw "manifest.items is missing" }

Write-Host "WS: $WsUrl"
Write-Host "Channel: $Channel"
Write-Host "Input: $InDir"
Write-Host ("Items: " + $manifest.items.Count)

$ws = [System.Net.WebSockets.ClientWebSocket]::new()
$uri = [Uri]::new($WsUrl)
$ws.ConnectAsync($uri, [Threading.CancellationToken]::None).GetAwaiter().GetResult()

# Join channel
$joinId = [Guid]::NewGuid().ToString()
$joinPayload = @{ id = $joinId; type = "join"; channel = $Channel } | ConvertTo-Json -Depth 4
Send-WsText -ws $ws -text $joinPayload

# Drain until we see join confirmation (best effort).
for ($i=0; $i -lt 10; $i++) {
  $t = Recv-WsText -ws $ws -timeoutMs 5000
  $o = $t | ConvertFrom-Json
  if ($o.type -eq "system" -and $o.channel -eq $Channel) { break }
}

$x = 0
$y = 0
$rowH = 0
$gap = 120
$maxRowW = 3200

foreach ($it in $manifest.items) {
  $file = Join-Path $InDir $it.file
  if (-not (Test-Path $file)) { throw "Missing PNG: $file" }

  $w = [int]$it.width
  $h = [int]$it.height
  $frameH = $h + 72

  if ($x -gt 0 -and ($x + $w) -gt $maxRowW) {
    $x = 0
    $y += ($rowH + $gap)
    $rowH = 0
  }

  $title = "$($it.name)  ($($it.path))"
  $url = $it.url

  Write-Host "Import: $title"

  $frame = Send-Command -ws $ws -channel $Channel -command "create_frame" -params @{
    x = $x; y = $y; width = $w; height = $frameH; name = $title;
    fillColor = @{ r = 0.96; g = 0.96; b = 0.96; a = 1.0 }
  }

  $null = Send-Command -ws $ws -channel $Channel -command "create_text" -params @{
    parentId = $frame.id; x = 16; y = 16; text = $url; fontSize = 14; fontWeight = 600;
    fontColor = @{ r = 0; g = 0; b = 0; a = 1.0 }
  }

  $pngBytes = [IO.File]::ReadAllBytes($file)
  $b64 = [Convert]::ToBase64String($pngBytes)

  $null = Send-Command -ws $ws -channel $Channel -command "create_image_rect" -params @{
    parentId = $frame.id; x = 0; y = 56; width = $w; height = $h; name = "Screen"; imageData = $b64
  } -timeoutMs 120000

  $x += ($w + $gap)
  $rowH = [Math]::Max($rowH, $frameH)
}

Write-Host "Done. Pages imported into the currently open Figma file."

