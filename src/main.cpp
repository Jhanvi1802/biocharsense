// BiocharSense firmware.
// Reads the kiln temperature (4-20 mA through an ADS1115) and the battery,
// stores each reading in flash and sends it over 4G (A7672S) to MQTT. A
// reading is deleted only after the broker confirms it (QoS 1).
// Topics: biocharsense/<id>/data (out), /cmd (in), /keepalive (out).
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "core.h"
#include "cycles.h"
#include "events.h"
#include "log.h"
#include "net.h"
#include "record_store.h"
#include "sensor.h"
#include "settings.h"
#include "timekeeper.h"

namespace {
Settings cfg;
Sensor sensor;
TimeKeeper clockUtc;
RecordStore store;
EventQueue events;
CycleManager cycles;
Net net;

uint16_t bootId = 0;
Sample lastSample{};
bool haveSample = false;
uint8_t lastFault = core::FAULT_OK;
uint8_t lowBattCount = 0;

uint32_t nextSampleMs = 0;
uint32_t nextUploadMs = 0;
uint32_t uploadFailures = 0;
bool uploadNow = false;
bool rebootPending = false;

const char* resetReasonName() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:  return "power_on";
        case ESP_RST_SW:       return "software";
        case ESP_RST_PANIC:    return "crash";
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:      return "watchdog";
        case ESP_RST_DEEPSLEEP:return "deep_sleep_wake";
        case ESP_RST_BROWNOUT: return "brownout";
        default:               return "other";
    }
}

uint16_t nextBootId() {
    Preferences p;
    p.begin("bs_sys", false);
    uint16_t id = p.getUShort("boot", 0) + 1;
    p.putUShort("boot", id);
    p.end();
    return id;
}

// Device health, sent on the keepalive topic every KEEPALIVE_S.
void fillHealth(JsonObject o) {
    if (clockUtc.valid()) o["ts"] = isoTime(clockUtc.now());
    o["fw"] = FW_VERSION;
    o["uptime_s"] = clockUtc.monoS();
    if (haveSample) {
        o["fault"] = core::faultName(lastSample.loop.fault);
        if (!isnan(lastSample.battV)) {
            o["batt_v"] = roundf(lastSample.battV * 100) / 100.0;
            o["batt_pct"] = core::liionPercent(lastSample.battV);
        }
    }
    o["csq"] = net.csq();
    o["operator"] = net.operatorName();
    o["queue"] = store.pending();
    o["free_heap"] = ESP.getFreeHeap();
    o["reset_reason"] = resetReasonName();
}

// Health plus diagnostics, for get_status and the serial console.
void fillStatus(JsonObject o) {
    fillHealth(o);
    o["boot"] = bootId;
    o["time_src"] = timeSourceName(clockUtc.source());
    if (haveSample) {
        if (!isnan(lastSample.loop.tempC)) o["temp_c"] = roundf(lastSample.loop.tempC * 10) / 10.0;
        if (!isnan(lastSample.loop.mA)) o["loop_ma"] = roundf(lastSample.loop.mA * 100) / 100.0;
    }
    o["simulated"] = cfg.simulate;
    o["sensor_ok"] = cfg.simulate || sensor.present();
    o["dropped"] = store.dropped();
    o["next_seq"] = store.tailSeq();
    JsonObject c = o["cycle"].to<JsonObject>();
    const auto& st = cycles.state();
    c["active"] = (bool)st.active;
    if (st.active) {
        c["cycle_id"] = cycles.cycleId(st.number);
        c["batch_id"] = st.batchId;
        if (st.startTs) c["start_ts"] = isoTime(st.startTs);
        c["readings"] = st.readings;
        if (st.peakDeci != INT16_MIN) c["peak_c"] = st.peakDeci / 10.0;
    }
}

void takeSample() {
    Sample s = sensor.read(cfg);
    const uint32_t now = clockUtc.now();
    const bool valid = !isnan(s.loop.tempC) && s.loop.fault != core::FAULT_SENSOR_BREAK;

    cycles.before(s.loop.tempC, valid, clockUtc.monoS(), now);

    Record r{};
    r.ts = now;
    r.monoS = clockUtc.monoS();
    r.cycleNo = cycles.activeNumber();
    r.bootId = bootId;
    r.tempDeci = isnan(s.loop.tempC) ? INT16_MIN : (int16_t)lroundf(s.loop.tempC * 10);
    r.loopCentiMa = isnan(s.loop.mA) ? 0 : (uint16_t)constrain(lroundf(s.loop.mA * 100), 0L, 65535L);
    r.battMv = isnan(s.battV) ? 0 : (uint16_t)lroundf(s.battV * 1000);
    r.fault = s.loop.fault;
    r.timeSrc = now ? clockUtc.source() : TS_NONE;
    store.append(r);
    cycles.after(r, now);

    // Fault changes become events so the dashboard can alert on them.
    if (s.loop.fault != lastFault) {
        JsonDocument d = events.make(s.loop.fault == core::FAULT_OK ? "fault_cleared" : "fault");
        d["fault"] = core::faultName(s.loop.fault);
        events.push(d);
        lastFault = s.loop.fault;
    }

    lastSample = s;
    haveSample = true;
    LOGI("sample #%u: %.1f C, %.2f mA, %s, batt %.2f V, cycle %u, queue %u",
         (unsigned)r.seq, s.loop.tempC, s.loop.mA, core::faultName(s.loop.fault),
         s.battV, (unsigned)r.cycleNo, (unsigned)store.pending());
}

void syncClock() {
    if (!clockUtc.needsSync(TIME_RESYNC_S)) return;
    uint32_t epoch;
    uint8_t src;
    if (net.readClock(epoch, src)) {
        clockUtc.set(epoch, src);
        LOGI("clock: %u UTC from %s", (unsigned)epoch, timeSourceName(src));
    } else {
        LOGW("clock: no network time yet");
    }
}

// Builds one data message from the oldest unsent readings.
bool buildBatch(String& out, uint32_t& lastSeq) {
    JsonDocument doc;
    doc["type"] = "reading";
    doc["device_id"] = cfg.deviceId;
    JsonArray arr = doc["readings"].to<JsonArray>();
    int n = 0;
    bool scanned = false;

    for (uint32_t seq = store.headSeq(); seq < store.tailSeq() && n < READINGS_PER_MSG; seq++) {
        lastSeq = seq;
        scanned = true;
        Record r;
        if (!store.read(seq, r)) continue;   // unreadable slot: skipped and acknowledged
        JsonObject o = arr.add<JsonObject>();
        o["seq"] = r.seq;
        uint32_t ts = r.ts;
        uint8_t src = r.timeSrc;
        if (!ts && r.bootId == bootId && clockUtc.valid()) {
            ts = core::backfillTs(clockUtc.syncEpoch(), clockUtc.syncMono(), r.monoS);
            if (ts) src = TS_BACKFILL;
        }
        if (ts) {
            o["ts"] = isoTime(ts);
            if (src != TS_NETWORK) o["src"] = timeSourceName(src);
        } else {                                  // time could not be worked out
            o["ts"] = nullptr;
            o["up_s"] = r.monoS;
            o["boot"] = r.bootId;
        }
        if (r.tempDeci != INT16_MIN) o["t"] = r.tempDeci / 10.0; else o["t"] = nullptr;
        if (r.battMv) {
            o["batt_v"] = roundf(r.battMv / 10.0f) / 100.0;
            o["batt_pct"] = core::liionPercent(r.battMv / 1000.0f);
        }
        if (r.fault != core::FAULT_OK) o["f"] = core::faultName(r.fault);
        if (r.cycleNo) o["c"] = r.cycleNo;      // cycle id = <device_id>-<c as 4 digits>
        // Never build a message larger than the MQTT buffer (it could then
        // never be sent): leave this reading for the next message.
        if (n > 0 && measureJson(doc) > MQTT_BUFFER_BYTES - 512) {
            arr.remove(arr.size() - 1);
            lastSeq = seq - 1;
            break;
        }
        n++;
    }
    if (!n) {
        if (scanned) store.ackThrough(lastSeq);
        return false;
    }
    serializeJson(doc, out);
    return true;
}

uint32_t lastKeepaliveMs = 0;

bool publishKeepalive() {
    if (lastKeepaliveMs && millis() - lastKeepaliveMs < KEEPALIVE_S * 1000UL) return true;
    JsonDocument d;
    d["type"] = "keepalive";
    d["device_id"] = cfg.deviceId;
    fillHealth(d.as<JsonObject>());
    String s;
    serializeJson(d, s);
    bool ok = net.publish("keepalive", s);
    if (ok) lastKeepaliveMs = millis();
    return ok;
}

// One upload pass. Returns true if everything was sent.
bool upload() {
    digitalWrite(PIN_STATUS_LED, HIGH);
    bool ok = net.connect();
    if (ok) {
        syncClock();
        ok = publishKeepalive();
        for (int m = 0; ok && m < MAX_MSGS_PER_UPLOAD && store.pending(); m++) {
            String payload;
            uint32_t lastSeq = 0;
            if (!buildBatch(payload, lastSeq)) continue;
            ok = net.publish("data", payload);
            if (ok) store.ackThrough(lastSeq);    // only after the broker's PUBACK
        }
        while (ok && !events.empty()) {
            ok = net.publish("data", events.front());
            if (ok) events.pop();
        }
    }
    digitalWrite(PIN_STATUS_LED, LOW);

    if (ok) {
        uploadFailures = 0;
        LOGI("upload ok, %u readings still queued", (unsigned)store.pending());
        if (!cfg.modemAlwaysOn) net.disconnect(true);
    } else {
        uploadFailures++;
        LOGW("upload failed (%u in a row): %s", (unsigned)uploadFailures, net.lastError().c_str());
        if (uploadFailures % MODEM_RESET_AFTER_FAILS == 0) net.restartModem();
    }
    return ok && store.pending() == 0;
}

uint32_t retryDelayS() {
    uint32_t d = RETRY_BASE_S << min<uint32_t>(uploadFailures - 1, 5);
    return min<uint32_t>(d, RETRY_MAX_S);
}

void handleCommand(const String& topic, const String& payload) {
    JsonDocument in;
    JsonDocument out;
    out["type"] = "ack";
    out["device_id"] = cfg.deviceId;
    bool ok = false;
    String err;

    if (deserializeJson(in, payload)) {
        err = "invalid JSON";
    } else {
        const char* cmd = in["cmd"] | "";
        out["id"] = in["id"] | "";
        out["cmd"] = cmd;
        LOGI("command: %s", cmd);

        if (!strcmp(cmd, "start_cycle")) {
            ok = cycles.start(in["batch_id"] | "", clockUtc.now(), err);
            if (ok) {
                out["cycle_id"] = cycles.cycleId(cycles.state().number);
                nextSampleMs = millis();          // first live reading right away
            }
        } else if (!strcmp(cmd, "end_cycle")) {
            ok = cycles.end(clockUtc.now(), "command", err);
        } else if (!strcmp(cmd, "get_status")) {
            fillStatus(out["status"].to<JsonObject>());
            ok = true;
        } else if (!strcmp(cmd, "get_config")) {
            settings::toJson(cfg, out["config"].to<JsonObject>());
            ok = true;
        } else if (!strcmp(cmd, "set_config")) {
            Settings next = cfg;
            ok = in["config"].is<JsonObject>();
            if (!ok) err = "missing config object";
            for (JsonPair kv : in["config"].as<JsonObject>()) {
                String v;
                if (kv.value().is<const char*>()) v = kv.value().as<const char*>();
                else serializeJson(kv.value(), v);
                if (!(ok = settings::apply(next, kv.key().c_str(), v, err))) break;
            }
            if (ok) {
                cfg = next;
                settings::save(cfg);
                cycles.configure(cfg);
                settings::toJson(cfg, out["config"].to<JsonObject>());
            }
        } else if (!strcmp(cmd, "resend")) {
            out["from_seq"] = store.rewind(in["from_seq"] | 0u);
            uploadNow = true;
            ok = true;
        } else if (!strcmp(cmd, "reboot")) {
            rebootPending = true;
            ok = true;
        } else {
            err = "unknown command";
        }
    }
    out["ok"] = ok;
    if (!ok) out["error"] = err;
    if (clockUtc.valid()) out["ts"] = isoTime(clockUtc.now());
    String s;
    serializeJson(out, s);
    net.publish("data", s);
    uploadNow = uploadNow || ok;      // send resulting events / config promptly
}

void checkBattery() {
    if (!haveSample || isnan(lastSample.battV) || lastSample.battV < 2.5f) return;  // not measured
    lowBattCount = lastSample.battV < cfg.battLowV ? lowBattCount + 1 : 0;
    if (lowBattCount < LOW_BATT_CONFIRM) return;

    LOGW("battery low (%.2f V): sending last data and sleeping", lastSample.battV);
    JsonDocument d = events.make("low_battery");
    d["batt_v"] = roundf(lastSample.battV * 100) / 100.0;
    events.push(d);
    upload();
    net.disconnect(true);
    store.sync();
    cycles.sync();
    Serial.flush();
    esp_sleep_enable_timer_wakeup((uint64_t)LOW_BATT_SLEEP_S * 1000000ULL);
    esp_deep_sleep_start();
}

void printJson(const JsonDocument& d) {
    serializeJsonPretty(d, Serial);
    Serial.println();
}

void handleConsole(String line) {
    line.trim();
    if (line.isEmpty()) return;
    int sp = line.indexOf(' ');
    String cmd = sp < 0 ? line : line.substring(0, sp);
    String rest = sp < 0 ? "" : line.substring(sp + 1);
    rest.trim();

    if (cmd == "help") {
        Serial.println(
            "status | config | set <key> <value> | upload | sample\n"
            "cycle start [batch_id] | cycle end | queue | queue clear\n"
            "at <command> | reboot | factory_reset\n"
            "keys: " + settings::keyList());
    } else if (cmd == "status") {
        JsonDocument d; fillStatus(d.to<JsonObject>()); printJson(d);
    } else if (cmd == "config") {
        JsonDocument d; settings::toJson(cfg, d.to<JsonObject>()); printJson(d);
    } else if (cmd == "set") {
        int s2 = rest.indexOf(' ');
        String key = s2 < 0 ? rest : rest.substring(0, s2);
        String val = s2 < 0 ? "" : rest.substring(s2 + 1);
        String err;
        if (settings::apply(cfg, key, val, err)) {
            settings::save(cfg);
            cycles.configure(cfg);
            Serial.println("saved: " + key + " (network settings apply after reboot)");
        } else {
            Serial.println("error: " + err);
        }
    } else if (cmd == "upload") {
        uploadNow = true;
    } else if (cmd == "sample") {
        takeSample();
    } else if (cmd == "cycle") {
        String err;
        bool ok;
        if (rest.startsWith("start")) ok = cycles.start(rest.substring(5).c_str() + (rest.length() > 5 ? 1 : 0), clockUtc.now(), err);
        else if (rest == "end") ok = cycles.end(clockUtc.now(), "console", err);
        else { ok = false; err = "use: cycle start [batch_id] | cycle end"; }
        Serial.println(ok ? "ok" : "error: " + err);
    } else if (cmd == "queue") {
        if (rest == "clear") { store.clear(); Serial.println("queue cleared"); }
        Serial.printf("pending %u, dropped %u, next seq %u\n", (unsigned)store.pending(),
                      (unsigned)store.dropped(), (unsigned)store.tailSeq());
    } else if (cmd == "at") {
        Serial.println(net.at(rest));
    } else if (cmd == "reboot") {
        rebootPending = true;
    } else if (cmd == "factory_reset") {
        settings::erase();
        store.clear();
        cycles.reset();
        Serial.println("settings, queue and cycles cleared; rebooting");
        rebootPending = true;
    } else {
        Serial.println("unknown command, type 'help'");
    }
}

void pollConsole() {
    static String buf;
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (buf.length()) handleConsole(buf);
            buf = "";
        } else if (buf.length() < 200) {
            buf += c;
        }
    }
}

// Status LED: one short blink every 2 s = connected, two = offline, solid = uploading.
void updateLed() {
    uint32_t t = millis() % 2000;
    bool on = t < 60 || (!net.connected() && t >= 250 && t < 310);
    digitalWrite(PIN_STATUS_LED, on);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(300);
    pinMode(PIN_STATUS_LED, OUTPUT);
    esp_task_wdt_init(WDT_TIMEOUT_S, true);
    esp_task_wdt_add(nullptr);

    settings::load(cfg);
    bootId = nextBootId();
    LOGI("BiocharSense %s, device %s, boot %u (%s)", FW_VERSION, cfg.deviceId,
         (unsigned)bootId, resetReasonName());
    if (cfg.simulate) LOGW("SIMULATION MODE: readings are generated, not measured");

    sensor.begin();
    store.begin();
    events.begin(cfg.deviceId, &clockUtc);
    cycles.begin(cfg, events);
    net.begin(cfg, handleCommand);

    JsonDocument d = events.make("boot");
    d["fw"] = FW_VERSION;
    d["reset_reason"] = resetReasonName();
    d["boot"] = bootId;
    events.push(d);

    takeSample();
    // Woken from a low-battery sleep and not recovered: sleep again without
    // switching the modem on (the 4G radio would drain the cell further). The
    // margin stops a cell that reads just above the limit at rest from
    // waking the modem, sagging under its load and going back to sleep.
    if (esp_reset_reason() == ESP_RST_DEEPSLEEP && !isnan(lastSample.battV) &&
        lastSample.battV > 2.5f && lastSample.battV < cfg.battLowV + LOW_BATT_RESUME_V) {
        LOGW("battery still low (%.2f V), sleeping again", lastSample.battV);
        Serial.flush();
        esp_sleep_enable_timer_wakeup((uint64_t)LOW_BATT_SLEEP_S * 1000000ULL);
        esp_deep_sleep_start();
    }
    nextSampleMs = millis() + cfg.sampleS * 1000UL;
    nextUploadMs = millis() + 5000;       // first upload soon: announce and sync the clock
}

void loop() {
    esp_task_wdt_reset();
    pollConsole();
    net.loop();

    const uint32_t now = millis();
    if ((int32_t)(now - nextSampleMs) >= 0) {
        takeSample();
        checkBattery();
        // Live mode: while a batch runs, sample faster and send each reading
        // straight away so the dashboard curve updates in real time.
        const bool live = cycles.activeNumber() != 0;
        nextSampleMs = now + (live ? cfg.liveSampleS : cfg.sampleS) * 1000UL;
        if (live && net.connected()) uploadNow = true;
    }
    if (uploadNow || (int32_t)(now - nextUploadMs) >= 0) {
        uploadNow = false;
        bool allSent = upload();
        uint32_t waitS = uploadFailures ? retryDelayS() : (allSent ? cfg.uploadS : 5);
        nextUploadMs = millis() + waitS * 1000UL;
    }
    if (net.connected()) publishKeepalive();
    if (rebootPending) {
        LOGI("rebooting");
        store.sync();
        cycles.sync();
        net.disconnect(false);
        Serial.flush();
        delay(500);
        ESP.restart();
    }
    updateLed();
    delay(10);
}
