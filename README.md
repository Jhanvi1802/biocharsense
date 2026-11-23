# BiocharSense

Battery-powered 4G temperature logger for Kon-Tiki biochar kilns. An ESP32 reads a
Tempsens K-type thermocouple through a 4-20 mA head transmitter and an ADS1115, stores
every reading in flash, and publishes it over 4G (SIMCom A7672S) to an MQTT broker.
Burn cycles are tied to Kalki batch IDs through two-way MQTT commands.

| Read this | For |
|---|---|
| [`docs/HANDOVER - BiocharSense IoT Device.docx`](docs/) | Knowledge-transfer document: hardware, firmware, data path, setup, testing, open issues |
| [`FIRMWARE.md`](FIRMWARE.md) | Build, wiring, MQTT topics, commands and settings for firmware v2 |
| `BiocharSense_MQTT_Packet_Reference.docx` | Field-by-field packet specification for the backend |
| `BiocharSense_System_Overview.docx` | Overview, cost and test plan |

## Quick start

```powershell
pio run -e esp32dev               # build
pio run -e esp32dev -t upload     # flash over USB
pio device monitor                # serial console, type: help
pio test -e native                # unit tests for lib/core
python tools/mqtt_console.py watch
```

`legacy_prototype/`, `ota_server/` and `ship-ota.ps1` belong to the earlier WiFi
prototype and are kept for reference only.

Before field use, replace the public test broker (`broker.emqx.io`) with a private
broker and per-device credentials.
