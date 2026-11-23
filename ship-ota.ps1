# One-command OTA shipper (ESP32 / esp32dev)
#   1) Bumps FW_VERSION patch number in include/appConfig.h
#   2) Builds firmware (env: esp32dev)
#   3) Stages .bin into ota_server/
#   4) Starts a local HTTP server on port 8000 if not already running
#   5) Publishes a RETAINED OTA command via MQTT (so a deep-sleeping device
#      receives it on its next wake window) and waits for the new version
#
# Usage:  .\ship-ota.ps1 [-Token <ota-token>]
# Requires: PlatformIO (pio), Python with paho-mqtt installed, device reachable.
#
# NOTE (deep-sleep model): the node only listens for commands during its short
# post-upload window (default every ~30 min). OTA latency is therefore up to one
# upload interval. The command is published RETAINED and cleared on success so
# the device does not re-flash on every wake.

param(
    [string]$Token = $env:VARAHA_OTA_TOKEN
)

$ErrorActionPreference = "Stop"
$projectRoot = $PSScriptRoot
Set-Location $projectRoot

$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
if (-not (Test-Path $pio)) { $pio = "pio" }   # fall back to PATH

# 1) Find LAN IP (first 192.168.* address)
$lanIp = (Get-NetIPAddress -AddressFamily IPv4 |
    Where-Object { $_.IPAddress -like '192.168.*' -and $_.PrefixOrigin -ne 'WellKnown' } |
    Select-Object -First 1).IPAddress
if (-not $lanIp) { Write-Error "Could not find LAN IP (192.168.x)"; exit 1 }
Write-Host "LAN IP            : $lanIp"

# 2) Read current FW_VERSION from appConfig.h and bump the patch number
$verFile = "include\appConfig.h"
$content = Get-Content $verFile -Raw
if ($content -notmatch 'FW_VERSION\s+"(\d+)\.(\d+)\.(\d+)"') {
    Write-Error "Could not find FW_VERSION in $verFile"
    exit 1
}
$major = [int]$Matches[1]; $minor = [int]$Matches[2]; $patch = [int]$Matches[3]
$oldVersion = "$major.$minor.$patch"
$newVersion = "$major.$minor.$($patch + 1)"
$content = $content -replace 'FW_VERSION\s+"[^"]+"', "FW_VERSION `"$newVersion`""
Set-Content -Path $verFile -Value $content -NoNewline
Write-Host "Bumped FW_VERSION : $oldVersion -> $newVersion"

# 3) Build
Write-Host "Building (esp32dev)..."
& $pio run -e esp32dev | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Error "Build failed"; exit 1 }
Write-Host "Built OK"

# 4) Stage .bin
$serveDir = Join-Path $projectRoot "ota_server"
if (-not (Test-Path $serveDir)) { New-Item -ItemType Directory -Path $serveDir | Out-Null }
$binName = "firmware-$newVersion.bin"
Copy-Item ".pio\build\esp32dev\firmware.bin" (Join-Path $serveDir $binName) -Force
Write-Host "Staged            : $binName"

# 5) Make sure HTTP server is running on port 8000
$listening = netstat -an | Select-String ":8000\s.*LISTENING"
if (-not $listening) {
    Write-Host "Starting HTTP server on :8000..."
    Start-Process py -ArgumentList @("-m","http.server","8000","--bind","0.0.0.0","--directory",$serveDir) -WindowStyle Hidden
    Start-Sleep -Seconds 2
} else {
    Write-Host "HTTP server already running on :8000"
}

# 6) Verify URL reachable from this machine
$url = "http://${lanIp}:8000/$binName"
try {
    $r = Invoke-WebRequest -Uri $url -Method Head -TimeoutSec 5 -UseBasicParsing
    Write-Host "URL reachable     : $url ($($r.Headers['Content-Length']) bytes)"
} catch {
    Write-Error "URL not reachable: $url -- $($_.Exception.Message)"
    exit 1
}

# 7) Trigger OTA via MQTT (retained), wait for the new version to appear
Write-Host "Triggering OTA via MQTT (retained)..."
$args = @("$serveDir\trigger_ota.py", $url, $newVersion)
if ($Token) { $args += @("--token", $Token) }
& py @args
if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "FAILED/timeout. Check: device reachable? broker reachable? URL fetched?"
    Write-Host "In the deep-sleep model the device may simply not have hit its wake"
    Write-Host "window yet — the retained command will be applied on the next wake."
    exit 1
}
Write-Host ""
Write-Host "Done. Device is now running $newVersion"
