#ifndef APP_CONFIG_H
#define APP_CONFIG_H

// =============================================================================
//  appConfig.h — Compile-time configuration for the Varaha / BiocharSense node.
//
//  This is the single source of truth for hardware pin mapping, firmware
//  version, timing defaults, and factory-provisioning values. Anything that a
//  field operator can change at runtime lives in DeviceConfig (configControl.h)
//  and is persisted to /config.json on LittleFS; the values here are the
//  compile-time DEFAULTS used to seed that file on a fresh device.
// =============================================================================

// ---- Firmware version -------------------------------------------------------
// Bumped by ship-ota.ps1. Reported in device-info and the SendConfigData reply.
#define FW_VERSION "1.0.7"

// ---- Hardware: MAX31855 K-type thermocouple amplifier (SPI, read-only) ------
// The MAX31855 is wired identically to the older MAX6675 (3-wire read-only SPI),
// so these pins are unchanged from the prototype. Bit-banged SPI via the
// Adafruit library — no hardware-SPI pins required.
#define PIN_TC_SCK   18   // SPI clock
#define PIN_TC_CS     5   // SPI chip-select
#define PIN_TC_SO    19   // SPI data out (MISO / DO on the breakout)

// ---- Hardware: setup button -------------------------------------------------
// Momentary push between PIN_SETUP_BTN and GND, active-LOW (internal pull-up).
// Must be an RTC-capable GPIO so it can wake the chip from deep sleep (ext0).
// GPIO4 is RTC_GPIO10 — valid. An external 100k pull-up improves deep-sleep
// wake reliability (the internal pull-up is weak while in RTC hold).
#define PIN_SETUP_BTN   4
#define SETUP_HOLD_MS   3000   // long-press duration to launch the config portal

// ---- Hardware: status LED ---------------------------------------------------
// Onboard LED on most ESP32 DevKits is GPIO2 (also a strapping pin; safe as a
// plain output once boot is complete). Set LED_ACTIVE_HIGH=0 for active-low.
#define PIN_STATUS_LED    2
#define LED_ACTIVE_HIGH   1

// ---- Sensor validity window -------------------------------------------------
// MAX31855 measurement range is -270..+1800 C. We clamp to a sane physical
// window for the kiln application; readings outside this are treated as faults.
#define TC_TEMP_MIN_C   (-50.0f)
#define TC_TEMP_MAX_C   (1372.0f)   // K-type upper limit per the MAX31855 datasheet

// ---- Power / duty-cycle defaults (seconds) ----------------------------------
// Deep-sleep model: the node wakes every SAMPLE_INTERVAL to read the probe and
// buffer the reading in RTC memory; every UPLOAD_INTERVAL it additionally brings
// up WiFi+MQTT to flush the batch and listen for commands. These are runtime
// configurable (DeviceConfig) so battery vs. resolution can be tuned in the
// field without reflashing.
#define DEFAULT_SAMPLE_INTERVAL_S    60      // probe sample cadence
#define DEFAULT_UPLOAD_INTERVAL_S    1800    // batch upload cadence (30 min)
#define DEFAULT_WAKE_WINDOW_MS       8000    // command/OTA listen window per upload

// WiFi TX power during the upload burst. Lower than the 19.5 dBm default to
// blunt the current spike that can brown out a draining 18650 when the radio
// keys up. Resolved in networkControl.cpp (where the WiFi enum is in scope).
#define WIFI_TX_POWER   WIFI_POWER_17dBm

// Hard bounds to stop a bad config from bricking the duty cycle.
#define SAMPLE_INTERVAL_MIN_S        5
#define SAMPLE_INTERVAL_MAX_S        3600
#define UPLOAD_INTERVAL_MIN_S        30
#define UPLOAD_INTERVAL_MAX_S        86400

// ---- RTC sample buffer ------------------------------------------------------
// Readings accumulated across deep-sleep cycles, flushed on each upload. Sized
// to survive a few missed upload windows (e.g. transient WiFi/broker outage)
// without losing data. 90 * 60 s ≈ 90 min of buffering at the default cadence.
#define RTC_SAMPLE_CAPACITY   90

// ---- Watchdog ---------------------------------------------------------------
// Task watchdog timeout for the active (awake) window. Must exceed the longest
// single blocking operation (WiFi/NTP/MQTT connect, and an OTA download). Deep
// sleep stops the WDT, so this only bounds the awake portion of each cycle.
#define WDT_TIMEOUT_S   90

// ---- Network timeouts (ms) --------------------------------------------------
#define WIFI_CONNECT_TIMEOUT_MS   15000
#define NTP_SYNC_TIMEOUT_MS       15000
#define MQTT_CONNECT_TIMEOUT_MS   10000
#define MQTT_SOCKET_TIMEOUT_S     15      // PubSubClient socket timeout
#define MQTT_KEEPALIVE_S          40
#define MQTT_BUFFER_SIZE          512     // must hold the largest JSON payload

// Earliest plausible epoch (2023-11-14) used to decide if NTP/RTC time is valid.
#define EPOCH_VALID_THRESHOLD   1700000000UL

// ---- Identity / protocol ----------------------------------------------------
// Canonical device-node tag carried in every payload ("DN"). The prototype was
// inconsistent ("TEMP" in data, "THEMP" in commands); production standardises on
// one value. The command handler also accepts the legacy "THEMP" for backward
// compatibility. NOTE: verify this matches your backend's ingest/command filter.
#define DEVICE_DN          "TEMP"
#define DEVICE_DN_LEGACY   "THEMP"

#define WIFI_HOSTNAME      "varaha-esp32"
#define SETUP_AP_NAME      "varaha-setup"

// POSIX TZ for IST (UTC+5:30, no DST). Sign is inverted in POSIX TZ notation.
#define TZ_IST   "IST-5:30"

// ---- Factory-default provisioning (seeded into /config.json once) -----------
// These are *defaults only*. Real deployments must re-provision via the portal.
// Do not ship a fleet that silently auto-joins a known default SSID/broker.
#define DEFAULT_WIFI_SSID     "YOUR_WIFI_SSID"
#define DEFAULT_WIFI_PASS     "YOUR_WIFI_PASSWORD"
#define DEFAULT_MQTT_BROKER   "broker.emqx.io"
#define DEFAULT_MQTT_PORT     1883
#define DEFAULT_MQTT_USER     ""
#define DEFAULT_MQTT_PASS     ""
#define DEFAULT_MQTT_TLS      false
#define DEFAULT_SUB_TOPIC     "varaha/Sub"
#define DEFAULT_PUB_TOPIC     "varaha/sensor"
#define DEFAULT_RESP_TOPIC    "varaha/Response"
#define DEFAULT_KEEP_TOPIC    "varaha/keepalive"

// ---- Commissioning ----------------------------------------------------------
// 1 (production): a fresh, unprovisioned unit opens the setup portal on first
// boot instead of auto-joining the compiled default SSID — so a fleet never
// silently joins a dev network. Set to 0 for bench/dev builds that should
// auto-join DEFAULT_WIFI_SSID without manual provisioning.
#define REQUIRE_PROVISIONING   1

// ---- OTA hardening ----------------------------------------------------------
// Shared secret that an OTA command MUST carry ("token" field) before the node
// will flash. Empty default = provision per device via config. An OTA URL host
// must also match OTA_HOST_ALLOW (substring match, empty = allow any — set this
// to your firmware origin in production).
#define DEFAULT_OTA_TOKEN        ""
#define DEFAULT_OTA_HOST_ALLOW   ""
#define OTA_MIN_FREE_HEAP        40000U   // refuse OTA below this free heap

#endif // APP_CONFIG_H
