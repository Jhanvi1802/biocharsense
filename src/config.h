#pragma once
// Pins, limits and factory defaults. Values that can be changed in the field
// (broker, intervals, transmitter range, shunt) are kept in NVS and set from
// the serial console or the set_config command; DEF_ values seed a new device.

#define FW_VERSION "2.0.0"

// Pin map: ESP32 DevKit V1
#define PIN_I2C_SDA        21   // ADS1115 SDA
#define PIN_I2C_SCL        22   // ADS1115 SCL
#define PIN_MODEM_RX       16   // ESP32 RX2  from modem TXD (through level shifter)
#define PIN_MODEM_TX       17   // ESP32 TX2  to modem RXD (through level shifter)
#define PIN_MODEM_PWRKEY   25   // to modem PWRKEY through 1 kOhm
#define PIN_STATUS_LED      2   // on-board LED

// 0: PWRKEY wired straight to the module (press = pull LOW).
// 1: modem board has an inverting transistor on PWRKEY (press = drive HIGH).
#define MODEM_PWRKEY_ACTIVE_HIGH 0
#define MODEM_BAUD         115200

// ADS1115
#define ADS_I2C_ADDR       0x48
#define ADS_CH_LOOP        0     // across the 100 Ohm shunt (via 1 kOhm)
#define ADS_CH_BATT        1     // midpoint of the 100k/100k battery divider
#define BATT_DIVIDER       2.0f
#define ADC_AVERAGE        8     // samples averaged per reading

// Storage
#define RECORD_CAPACITY    8000  // ~5.5 days of 1-minute readings
#define EVENT_QUEUE_LEN    16
#define META_SAVE_MS       30000 // queue position is saved to NVS at most this often
#define RECORD_MAGIC       0xA5

// Network / upload
#define TOPIC_ROOT         "biocharsense"
#define MQTT_BUFFER_BYTES  4096
#define MQTT_KEEPALIVE_S   120
#define MQTT_TIMEOUT_MS    20000
#define READINGS_PER_MSG   20    // ~2 kB typical, worst case ~3 kB: fits the MQTT buffer
#define MAX_MSGS_PER_UPLOAD 30
#define NETWORK_TIMEOUT_MS 120000
#define TIME_RESYNC_S      21600 // re-sync the clock every 6 h
#define RETRY_BASE_S       60    // first retry after a failed upload
#define RETRY_MAX_S        1800  // retries back off up to 30 min
#define MODEM_RESET_AFTER_FAILS 3

// Power
#define LOW_BATT_CONFIRM   3     // consecutive low readings before shutdown
#define LOW_BATT_SLEEP_S   3600  // sleep, then re-check the battery
#define LOW_BATT_RESUME_V  0.10f // after a low-battery sleep, wake only above batt_low_v + this

// Watchdog
#define WDT_TIMEOUT_S      180   // longer than the slowest network step

// Factory defaults (stored in NVS on first boot)
#define DEF_MQTT_HOST      "broker.emqx.io"   // public test broker: replace for production
#define DEF_MQTT_PORT      8883
#define DEF_MQTT_TLS       true
#define DEF_SAMPLE_S       60
#define DEF_LIVE_SAMPLE_S  10       // during a batch: sample this often and send each reading at once
#define KEEPALIVE_S        60       // health packet interval
#define TZ_OFFSET_MIN      330      // times in messages are shown in IST (UTC+05:30)
#define DEF_UPLOAD_S       300
#define DEF_RANGE_MIN_C    0.0f
#define DEF_RANGE_MAX_C    200.0f   // Tempsens transmitter as delivered; set 1200 after re-ranging
#define DEF_SHUNT_OHMS     100.0f   // measure the real resistor and set it here
#define DEF_CYCLE_START_C  100.0f
#define DEF_CYCLE_END_C    80.0f
#define DEF_CYCLE_HOLD_S   1800     // below end temperature for 30 min = burn over
#define DEF_CYCLE_CONFIRM  3
#define DEF_BATT_LOW_V     3.45f    // modem stops working below ~3.4 V
