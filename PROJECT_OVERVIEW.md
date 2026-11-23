# Varaha / BiocharSense Sensor Node — System Overview

Production firmware for a battery-powered, WiFi-connected K-type thermocouple
node that monitors a biochar (Kontiki) kiln. It samples the probe on a deep-sleep
duty cycle, batch-publishes readings to MQTT, and supports field re-provisioning
and guarded over-the-air firmware updates.

> **Note:** This document describes the current ESP32 production firmware. An
> earlier prototype targeted the ESP8266 and ran always-on; that is no longer the
> case. See §13 for what changed.

---

## 1. Summary

The node is built on an **ESP32 (WROOM-32, `board = esp32dev`)** and reads a
**MAX31855** K-type thermocouple amplifier over SPI. To meet the battery budget,
it runs a **deep-sleep duty cycle**:

- Every **sample interval** (default 60 s) it wakes, reads the probe, appends the
  reading to a buffer in **RTC memory** (survives deep sleep), and sleeps again.
- Every **upload interval** (default 30 min) it additionally brings up WiFi+MQTT,
  flushes the whole batch, listens briefly for commands/OTA, then sleeps.

It exposes two remote channels, both available during the post-upload wake window:

- A **WiFi + MQTT + OTA configuration portal** (captive-portal SoftAP) for field
  commissioning without a computer.
- A **guarded OTA update** (token + host allowlist + version guard), triggered by
  a (retained) MQTT command.

---

## 2. Feature Inventory

| Area | Feature | How it works |
|---|---|---|
| Sensing | K-type thermocouple | MAX31855 SPI; full K-type range + open/short fault detection + cold-junction temp |
| Power | Deep-sleep duty cycle | Sample → RTC buffer → sleep; WiFi only during the batch upload window |
| Power | Brownout mitigation | WiFi TX power capped (17 dBm) to blunt the radio current spike on battery |
| Reliability | Task watchdog | Armed during the awake window (90 s, panic-reset); deep sleep stops it |
| Reliability | Bounded networking | WiFi/NTP/MQTT connects are all timeout-bounded — no infinite retry loops |
| Connectivity | WiFi STA | SSID/password from `/config.json` (LittleFS) |
| Time | NTP sync (IST) | POSIX TZ `IST-5:30`; time persists across deep sleep once synced |
| Location | IP geolocation | One HTTP call to `ip-api.com`, cached in RTC across sleeps |
| Telemetry | Batched sensor publish | All buffered samples flushed each upload, each keeping its own timestamp |
| Telemetry | Status heartbeat | One per upload: RSSI, free heap, boot count, FW version |
| Provisioning | Setup portal | Open AP `varaha-setup`, captive portal at 192.168.4.1; sets **WiFi + broker + MQTT creds + OTA token** |
| Provisioning | First-boot gate | An unprovisioned unit forces the portal instead of joining the dev SSID (`REQUIRE_PROVISIONING`) |
| Provisioning | Portal triggers | (a) Serial `portal`, (b) GPIO4 button (deep-sleep wake + hold), (c) unprovisioned/first boot |
| Persistence | Config storage | `/config.json` on LittleFS; survives OTA, wiped only by full flash erase |
| Remote ops | MQTT commands | `varaha/Sub`: `SendConfigData` (redacted) and `ota` |
| Remote ops | Guarded OTA | Shared-secret token + host allowlist + heap precheck + HTTPS-capable + anti-reflash version guard |
| Safety | Boot-health rollback hook | `esp_ota_mark_app_valid` once the network proves the new image healthy |
| Versioning | FW version | Compile-time `FW_VERSION` (appConfig.h), reported in device-info/status |
| Diagnostics | Verbose boot log | Reset reason, wake cause, boot count, free heap, redacted config dump |
| Testability | Host unit tests | Pure logic in `coreLogic.h` tested via `pio test -e native` |

---

## 3. System Architecture

```
                                                  +----------------------------+
                                                  |   MQTT Broker              |
                                                  |   (default broker.emqx.io  |
                                                  |    :1883 — replace for prod|
                                                  |    with a private/TLS one) |
                                                  +-------------+--------------+
                                                                |
                                  PUB varaha/sensor             |   SUB varaha/Sub  (commands)
                                  PUB varaha/keepalive (status) |       - SendConfigData
                                  PUB varaha/Response           |       - ota (token-guarded)
                                                                v
+---------------------------+               +-----------------------------------+
|  Backend / Dashboard /    | <-- MQTT ---> |        ESP32 (WROOM-32)           |
|  Operator                 |               |                                   |
+---------------------------+               | +-------------------------------+ |
                                            | |  Firmware                     | |
+---------------------------+               | |  - main.cpp   (duty cycle)    | |
|  HTTPS/HTTP firmware origin| <- HTTP(S) - | |  - powerControl (deep sleep)  | |
|  (S3 / GitHub Releases /  |   (fetch     | |  - networkControl (WiFi/NTP)  | |
|  local nginx)             |   firmware)  | |  - mqttControl  (pub/sub)     | |
|  Hosts firmware-X.Y.Z.bin |               | |  - deviceControl (MAX31855)   | |
+---------------------------+               | |  - configControl (LittleFS)   | |
                                            | |  - otaControl   (guarded OTA) | |
                                            | +-------------------------------+ |
                                            |                                   |
                                            |  GPIO19 (SO)  <-- MAX31855 --+    |
                                            |  GPIO5  (CS)                 |    |
                                            |  GPIO18 (SCK)                |    |
                                            |  GPIO4  <-- Setup button (RTC wake)|
                                            |  GPIO2  --> Status LED            |
                                            +-----------------+-----------------+
                                                              |
                                                          K-type
                                                          thermocouple
```

---

## 4. Data Flow

### 4.1 Wake sequence (every wake is a fresh boot)
```
1. Serial up; print FW version, reset reason, free heap
2. powerInit(): classify wake (COLD_BOOT / TIMER / BUTTON), (re)init RTC state
3. Arm task watchdog (90 s)
4. Mount LittleFS, load /config.json (seed defaults if missing)
5. Init status LED, setup button, MAX31855
6. If BUTTON wake -> confirm 3 s hold -> setup portal (does not return)
7. If not provisioned (REQUIRE_PROVISIONING) -> setup portal (does not return)
8. Read probe -> append reading to RTC sample buffer
9. If upload due (cold boot / time unsynced / buffer full / interval elapsed):
        -> run upload cycle (§4.2)
   else -> just go to sleep
10. Deep sleep for the sample interval (timer + button wake armed)
```

### 4.2 Upload cycle
```
connectWifi (<=15 s)            -- fail -> keep buffer, sleep, retry next cycle
syncTimeNtp (<=15 s)            -- on success, back-fill the cold-boot sample's timestamp
otaMarkAppValid()              -- network healthy -> confirm this image (no rollback)
ensureGeolocation()           -- once, cached in RTC
mqttBegin + mqttConnect (<=10 s)-- fail -> keep buffer, sleep, retry next cycle
publishDeviceInfo()           -- only on cold boot
flush all buffered samples -> varaha/sensor   (each with its own timestamp)
publish status -> varaha/keepalive
command/OTA listen window (default 8 s): service MQTT + serial
disconnect, WiFi off, clear buffer, stamp last-upload time, deep sleep
```

### 4.3 Inbound MQTT command flow
```
Broker --(varaha/Sub)--> mqtt callback()
   Parse JSON; require DN == "TEMP" (legacy "THEMP" also accepted)
     command = "SendConfigData"  (legacy "SendCongigData" accepted)
         -> publish REDACTED config to varaha/Response (no passwords/token)
     command = "ota"
         -> if version == running FW_VERSION: ignore (anti-reflash)
         -> if otaToken set and token mismatch: reject
         -> performOtaUpdate(url): host allowlist + heap check + HTTP(S) flash + reboot
```

### 4.4 Setup portal flow
```
Trigger (serial "portal" | button hold | unprovisioned first boot)
   -> SoftAP "varaha-setup", captive portal at 192.168.4.1
   -> Operator enters WiFi + (optional) MQTT broker/port/user/pass + OTA token
   -> On save: persist to /config.json, set provisioned=true, restart
```

### 4.5 OTA update flow (deep-sleep aware)
```
Backend publishes RETAINED to varaha/Sub:
  { "DN":"TEMP", "command":"ota", "url":"http(s)://host/firmware-X.Y.Z.bin",
    "version":"X.Y.Z", "token":"<shared-secret>" }
   -> Device receives it on its next wake window (latency up to one upload interval)
   -> Guards pass -> httpUpdate streams to the inactive OTA partition -> verify -> reboot
   -> New image confirmed healthy on next successful network bring-up
   -> Publisher (or trigger_ota.py) clears the retained command after success
```

---

## 5. MQTT Topics and Payloads

| Topic | Direction | When | Payload |
|---|---|---|---|
| `varaha/Sub` | Device subscribes | On demand (retained ok) | `{"DN":"TEMP","command":"...","url":"...","version":"...","token":"..."}` |
| `varaha/sensor` | Device publishes | Cold boot + each upload | Boot: `{"DN":"TEMP","MAC_ID":"...","IP":"...","CHIP_No":"...","fw_version":"1.0.7"}`<br>Sample: `{"DN":"TEMP","Temperature":312.5,"Internal":29.3,"Fault":"OK","Timestamp":"2026-06-27 14:43:32","Lat":28.65,"Lon":77.24,"Location":"..."}` |
| `varaha/keepalive` | Device publishes | Once per upload | `{"DN":"TEMP","type":"status","fw_version":"1.0.7","rssi":-61,"free_heap":210000,"boot":42,"Timestamp":"..."}` |
| `varaha/Response` | Device publishes | On `SendConfigData` | Redacted config (SSID, broker, port, topics, intervals, version — **no passwords/token**) |

`Temperature`/`Internal` serialise as `null` when the reading is faulted; `Fault`
carries the reason (`OK`, `OPEN`, `SHORT_GND`, `SHORT_VCC`, `RANGE`, `NAN`, or a
`|`-joined combination).

---

## 6. Hardware

| Component | Model | Pin (ESP32 GPIO) |
|---|---|---|
| MCU board | ESP32 DevKit (WROOM-32) | — |
| Temperature sensor | MAX31855 K-type amplifier + probe | SCK = GPIO18, CS = GPIO5, SO = GPIO19 |
| Setup button | Momentary to GND, `INPUT_PULLUP` | GPIO4 (RTC-capable; deep-sleep wake) |
| Status LED | Onboard LED | GPIO2 (`LED_ACTIVE_HIGH` configurable) |
| Power | 18650 Li-ion + Type-C charge module | — |

> **Recommended:** add an external ~100 kΩ pull-up on GPIO4 — the internal pull-up
> is weak while the pad is held in RTC mode during deep sleep.

---

## 7. Configuration (`/config.json` on LittleFS)

```json
{
  "ssid": "YOUR_WIFI_SSID",
  "pass": "********",
  "broker": "broker.emqx.io",
  "port": 1883,
  "user": "",
  "mqtpass": "********",
  "tls": false,
  "subtop": "varaha/Sub",
  "pubtop": "varaha/sensor",
  "resptop": "varaha/Response",
  "keep": "varaha/keepalive",
  "otatok": "",
  "otahost": "",
  "smpl": 60,
  "upl": 1800,
  "wake": 8000,
  "prov": false
}
```

Compile-time defaults (`include/appConfig.h`) seed this file once, on fresh flash.
Intervals are clamped to safe bounds on load. OTA does not touch LittleFS, so
device-specific config survives firmware upgrades.

---

## 8. Security Posture

| Concern | State | Notes / remaining work |
|---|---|---|
| MQTT auth | Username/password supported | Provision per device; default broker is public — move to a private one |
| MQTT transport | TLS-capable (`tls:true`) | Currently `setInsecure()` — **provision a CA** (`setCACert`) before trusting a broker |
| OTA authorisation | Token + host allowlist + version guard | Set `otatok` and `otahost` per device |
| OTA transport | HTTP and HTTPS | HTTPS uses `setInsecure()` until a CA is pinned |
| OTA integrity | Not signed | For supply-chain integrity, add image signing / secure boot |
| Config exposure | Passwords/token redacted | `SendConfigData` never returns secrets; secrets never logged verbatim |
| Portal AP | Open | Optionally password the AP via `startConfigPortal(ssid, pass)` |
| Rollback | Hook present | True auto-rollback needs `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` (custom sdkconfig) |

---

## 9. Project Structure

```
varaha-main/
├── platformio.ini              # esp32dev (firmware) + native (host tests) envs
├── ship-ota.ps1                # One-command OTA shipper (esp32dev)
├── include/
│   ├── appConfig.h             # Central config: pins, intervals, version, defaults, flags
│   ├── coreLogic.h             # Pure, Arduino-free helpers (unit-tested)
│   ├── configControl.h         # DeviceConfig struct + LittleFS API
│   ├── deviceControl.h         # MAX31855 + button + LED
│   ├── powerControl.h          # Deep-sleep duty cycle + RTC sample buffer
│   ├── networkControl.h        # WiFi + NTP + geoloc + portal
│   ├── mqttControl.h           # MQTT pub/sub + command dispatch
│   └── otaControl.h            # Guarded OTA + boot-health
├── src/
│   ├── main.cpp                # Wake → sample → conditional upload → sleep
│   ├── configControl.cpp
│   ├── deviceControl.cpp
│   ├── powerControl.cpp
│   ├── networkControl.cpp
│   ├── mqttcontrol.cpp
│   └── otaControl.cpp
├── test/
│   └── test_core/test_core.cpp # Host unit tests (clamp, ring buffer, URL parsing)
├── min_lfs_ota.csv             # Partition table (2× OTA app slots + LittleFS)
└── ota_server/
    ├── firmware-X.Y.Z.bin
    └── trigger_ota.py          # Retained OTA publisher + post-reboot verifier
```

---

## 10. Build, Flash, Test, Monitor

```powershell
# Compile firmware
pio run -e esp32dev

# Build + upload over USB
pio run -e esp32dev -t upload

# Host unit tests (no hardware needed)
pio test -e native

# Static analysis
pio check -e esp32dev

# Serial monitor (115200, with panic backtrace decoding)
pio device monitor -b 115200

# Full erase (wipes LittleFS config; forces defaults + re-provision)
pio run -e esp32dev -t erase

# Ship a new firmware version over the air
.\ship-ota.ps1 -Token <ota-token>
```

---

## 11. Provisioning a New Device

1. Flash firmware once over USB (on USB/mains power — a fresh unit will open the portal).
2. On first boot the unit is unprovisioned, so it opens the `varaha-setup` AP.
3. Connect a phone to `varaha-setup`; the captive portal opens.
4. Select the site WiFi, enter the password, and (optionally) set the MQTT broker,
   port, username/password, and OTA token. Save.
5. The unit persists the config, marks itself provisioned, reboots, and begins its
   duty cycle.

To re-provision later: hold the GPIO4 button (wakes the device, then hold 3 s) or
send `portal` over serial.

---

## 12. Shipping a Firmware Update

```powershell
.\ship-ota.ps1 -Token <ota-token>
```

The script bumps `FW_VERSION` in `appConfig.h`, builds, stages the `.bin`, serves
it locally, and publishes a **retained** `ota` command (with `version` + `token`).
Because the device only listens during its wake window, OTA latency is up to one
upload interval. On seeing the new version, the script clears the retained command
so the device does not re-flash on subsequent wakes (the device also guards against
this via the version check). For production, replace the local server with a hosted
origin and the script with your backend's MQTT publisher.

---

## 13. What Changed From the Prototype

- **MCU:** ESP8266/NodeMCU → **ESP32 (esp32dev)**; pins remapped (SCK18/CS5/SO19).
- **Sensor:** MAX6675 (0–1024 °C, open-only fault) → **MAX31855** (full K-type range,
  open/short faults, cold-junction).
- **Power:** always-on (publish every 10 s, keepalive every 5 s) → **deep-sleep duty
  cycle** with RTC-buffered batched uploads.
- **Reliability:** removed the blocking infinite MQTT reconnect loop; added a task
  watchdog and bounded connect timeouts.
- **Memory:** replaced the leaking `strdup`-based `const char*` globals with a
  leak-free `DeviceConfig` struct.
- **Security:** redacted secrets, token+allowlist+version-guarded OTA, TLS-capable
  transport, forced first-boot provisioning, boot-health rollback hook.
- **Tooling:** `ship-ota.ps1` fixed to `esp32dev`; added host unit tests.
```
