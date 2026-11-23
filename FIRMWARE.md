# BiocharSense firmware v2

ESP32 firmware for the 4G kiln temperature monitor. It reads the Tempsens
4-20 mA transmitter through an ADS1115, stores every reading in flash, and
sends the readings over 4G (SIMCom A7672S) to an MQTT broker. Kalki can send
commands back to the device. The previous WiFi prototype is in `legacy_prototype/`.

## Build, flash, test

```powershell
pio run -e esp32dev               # build
pio run -e esp32dev -t upload     # flash over USB
pio device monitor                # serial console, type: help
pio test -e native                # unit tests for lib/core (runs on the PC)
python tools/mqtt_console.py watch    # watch what devices send (pip install paho-mqtt)
```

## Libraries used

| Part | Library |
|---|---|
| ADS1115 ADC | Adafruit ADS1X15 |
| A7672S 4G modem (network, data, TLS, clock) | TinyGSM 0.12 (`TINY_GSM_MODEM_A7672X`) |
| MQTT (QoS 1, persistent session, last will) | 256dpi arduino-mqtt |
| JSON | ArduinoJson 7 |
| Settings and storage | ESP32 Preferences (NVS), LittleFS |

## Code layout

| File | What it does |
|---|---|
| `src/main.cpp` | Schedules sampling and uploads; status, commands, serial console, low battery |
| `src/sensor.*` | 4-20 mA and battery readings via ADS1115; simulation mode |
| `src/net.*` | 4G connection and MQTT (TinyGSM + arduino-mqtt) |
| `src/record_store.*` | Store-and-forward queue of readings in flash |
| `src/cycles.*` | Burn cycles and their Kalki batch IDs |
| `src/events.h` | Queue of event messages |
| `src/settings.*` | Field settings in NVS |
| `src/config.h` | Pins, limits, factory defaults |
| `lib/core/core.h` | Hardware-free logic (conversion, faults, ring buffer, cycle detection, time), unit-tested |

## Wiring (ESP32 DevKit V1)

| ESP32 | Connects to |
|---|---|
| GPIO21 / GPIO22 | ADS1115 SDA / SCL |
| GPIO16 (RX2) | Modem TXD (through level shifter if the modem uses 1.8 V) |
| GPIO17 (TX2) | Modem RXD (through level shifter if needed) |
| GPIO25 | Modem PWRKEY through 1 kOhm |
| GPIO2 | Status LED (on board) |
| ADS1115 A0 | Shunt top (100 Ohm to GND) through 1 kOhm; zener + 100 nF to GND |
| ADS1115 A1 | Battery divider midpoint (100k / 100k) |

If the modem board has a transistor on PWRKEY, set `MODEM_PWRKEY_ACTIVE_HIGH 1` in `config.h`.

## How data flows

1. Every `sample_s` (default 60 s): read the loop current, convert to °C, flag
   faults (NAMUR NE43: < 3.6 mA loop open, > 21 mA thermocouple broken).
2. The reading gets a sequence number and the time (UTC from the 4G network or
   NTP). Readings taken before the clock is known are back-filled exactly
   once it is, as long as the device has not rebooted in between.
3. It is written to flash (8,000 readings, about 5.5 days at 1 per minute).
4. Every `upload_s` (default 300 s): connect, sync the clock, publish status,
   then publish stored readings 30 per message with QoS 1. A reading is
   removed only after the broker's PUBACK. Failed uploads retry after
   60, 120, 240 ... up to 1800 s; the modem is restarted every 3 failures.

## MQTT topics

Three topics per device. Every packet has a `type` field saying what it is.

| Topic | Direction | Content |
|---|---|---|
| `biocharsense/<id>/data` | device to cloud | `reading` (every 10 s), batch `cycle_start` / `cycle_end` / `cycle_update`, `boot`, `fault`, `fault_cleared`, `low_battery`, and `ack` (reply to each command) |
| `biocharsense/<id>/cmd` | cloud to device | Commands (QoS 1, held by the broker while the device is offline) |
| `biocharsense/<id>/keepalive` | device to cloud | `keepalive` health packet every 60 s; `online` / `offline` (retained, offline is the last will) |

Reading (on `data`):

```json
{"type":"reading","device_id":"BS-1A2B3C",
 "readings":[{"seq":381,"ts":"2026-10-22 11:13:02","t":612.4,"batt_v":3.86,"batt_pct":62,"c":142}]}
```

Keepalive (on `keepalive`):

```json
{"type":"keepalive","device_id":"BS-1A2B3C","ts":"2026-10-22 11:13:00","fw":"2.0.0",
 "uptime_s":86400,"fault":"OK","batt_v":3.86,"batt_pct":62,"csq":19,
 "operator":"airtel","queue":0,"free_heap":186000,"reset_reason":"power_on"}
```

`ts` is India time (IST, UTC+05:30), as `YYYY-MM-DD HH:MM:SS`. `c` is the batch (cycle) number, only
while a batch runs. `src` appears only when the time came from NTP or was
back-filled, `f` only on a sensor fault (then `t` is null). If the time could
not be worked out, `ts` is null and `up_s` + `boot` are sent instead.
Deduplicate on `device_id` + `seq`.

## Commands

Send JSON to `biocharsense/<id>/cmd`; the reply (`"type":"ack"`) comes on `data` and repeats `id`.

| Command | Fields | Effect |
|---|---|---|
| `start_cycle` | `batch_id` | Starts a burn cycle for the batch, or attaches the batch to an auto-detected one |
| `end_cycle` | | Ends the cycle and sends a `cycle_end` summary event |
| `get_status` | | Returns the status object |
| `get_config` | | Returns the settings (no password) |
| `set_config` | `config: {key: value}` | Changes settings (validated; network settings apply after reboot) |
| `resend` | `from_seq` | Sends readings again from that sequence number |
| `reboot` | | Restarts the device |

Example: `{"id":"a1","cmd":"start_cycle","batch_id":"KT-114-2026-10-21"}`

## Settings (serial `set <key> <value>` or `set_config`)

`device_id`, `apn` (empty = automatic for Airtel / Jio / Vi / BSNL), `mqtt_host`,
`mqtt_port`, `mqtt_tls`, `mqtt_user`, `mqtt_pass`, `sample_s`, `upload_s`,
`range_min`, `range_max` (transmitter range; **200 as delivered, set 1200 after
Tempsens re-ranges it**), `shunt_ohm` (**set to the measured value of the 100 Ohm
resistor**), `cycle_start_c`, `cycle_end_c`, `cycle_hold_s`, `batt_low_v`,
`allow_roaming` (off: refuses non-Indian networks near borders),
`modem_always_on`, `simulate`.

## Testing before the sensor arrives

With only the ESP32 and the modem connected:

```
set simulate 1
reboot
```

The device then generates a 4-hour burn curve, auto-detects cycles, and sends
everything over 4G. Watch it with `python tools/mqtt_console.py watch`.

## Before field use

- Replace the public test broker (`broker.emqx.io`) with Varaha's own broker,
  with a username and password per device.
- TLS currently encrypts traffic but does not verify the broker's certificate.
  Load the broker's CA certificate into the modem and enable verification.
- Over-the-air firmware updates are not included yet.
