#include "settings.h"
#include <Preferences.h>
#include "config.h"

namespace {
const char* NS = "bs_cfg";

void copyStr(char* dst, size_t n, const String& v) {
    strncpy(dst, v.c_str(), n - 1);
    dst[n - 1] = 0;
}

String defaultDeviceId() {
    uint64_t mac = ESP.getEfuseMac();   // last 3 bytes of the MAC identify the board
    char buf[16];
    snprintf(buf, sizeof buf, "BS-%02X%02X%02X",
             (uint8_t)(mac >> 24), (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
    return String(buf);
}

bool toUint(const String& v, uint32_t lo, uint32_t hi, uint32_t& out) {
    if (v.isEmpty()) return false;
    for (char c : v) if (!isDigit(c)) return false;
    unsigned long x = strtoul(v.c_str(), nullptr, 10);
    if (x < lo || x > hi) return false;
    out = x;
    return true;
}

bool toFloat(const String& v, float lo, float hi, float& out) {
    char* end = nullptr;
    float x = strtof(v.c_str(), &end);
    if (end == v.c_str() || *end != 0 || isnan(x) || x < lo || x > hi) return false;
    out = x;
    return true;
}

bool toBool(const String& v, bool& out) {
    if (v == "1" || v == "true" || v == "on")  { out = true;  return true; }
    if (v == "0" || v == "false" || v == "off") { out = false; return true; }
    return false;
}

bool toText(const String& v, char* dst, size_t n, bool allowEmpty) {
    if ((!allowEmpty && v.isEmpty()) || v.length() >= n) return false;
    copyStr(dst, n, v);
    return true;
}
}  // namespace

namespace settings {

void load(Settings& s) {
    Preferences p;
    p.begin(NS, true);   // read-only; getters return the defaults if absent
    copyStr(s.deviceId, sizeof s.deviceId, p.getString("id", defaultDeviceId()));
    copyStr(s.apn, sizeof s.apn, p.getString("apn", ""));
    copyStr(s.mqttHost, sizeof s.mqttHost, p.getString("host", DEF_MQTT_HOST));
    s.mqttPort      = p.getUShort("port", DEF_MQTT_PORT);
    s.mqttTls       = p.getBool("tls", DEF_MQTT_TLS);
    copyStr(s.mqttUser, sizeof s.mqttUser, p.getString("user", ""));
    copyStr(s.mqttPass, sizeof s.mqttPass, p.getString("pass", ""));
    s.sampleS       = p.getUInt("sample", DEF_SAMPLE_S);
    s.uploadS       = p.getUInt("upload", DEF_UPLOAD_S);
    s.liveSampleS   = p.getUInt("lsample", DEF_LIVE_SAMPLE_S);
    s.rangeMinC     = p.getFloat("rmin", DEF_RANGE_MIN_C);
    s.rangeMaxC     = p.getFloat("rmax", DEF_RANGE_MAX_C);
    s.shuntOhms     = p.getFloat("shunt", DEF_SHUNT_OHMS);
    s.cycleStartC   = p.getFloat("cstart", DEF_CYCLE_START_C);
    s.cycleEndC     = p.getFloat("cend", DEF_CYCLE_END_C);
    s.cycleHoldS    = p.getUInt("chold", DEF_CYCLE_HOLD_S);
    s.battLowV      = p.getFloat("blow", DEF_BATT_LOW_V);
    s.allowRoaming  = p.getBool("roam", false);
    s.modemAlwaysOn = p.getBool("alwayson", true);
    s.simulate      = p.getBool("sim", false);
    p.end();
}

void save(const Settings& s) {
    Preferences p;
    p.begin(NS, false);
    p.putString("id", s.deviceId);
    p.putString("apn", s.apn);
    p.putString("host", s.mqttHost);
    p.putUShort("port", s.mqttPort);
    p.putBool("tls", s.mqttTls);
    p.putString("user", s.mqttUser);
    p.putString("pass", s.mqttPass);
    p.putUInt("sample", s.sampleS);
    p.putUInt("upload", s.uploadS);
    p.putUInt("lsample", s.liveSampleS);
    p.putFloat("rmin", s.rangeMinC);
    p.putFloat("rmax", s.rangeMaxC);
    p.putFloat("shunt", s.shuntOhms);
    p.putFloat("cstart", s.cycleStartC);
    p.putFloat("cend", s.cycleEndC);
    p.putUInt("chold", s.cycleHoldS);
    p.putFloat("blow", s.battLowV);
    p.putBool("roam", s.allowRoaming);
    p.putBool("alwayson", s.modemAlwaysOn);
    p.putBool("sim", s.simulate);
    p.end();
}

void erase() {
    Preferences p;
    p.begin(NS, false);
    p.clear();
    p.end();
}

String keyList() {
    return "device_id apn mqtt_host mqtt_port mqtt_tls mqtt_user mqtt_pass sample_s upload_s live_sample_s "
           "range_min range_max shunt_ohm cycle_start_c cycle_end_c cycle_hold_s batt_low_v "
           "allow_roaming modem_always_on simulate";
}

bool apply(Settings& s, const String& key, const String& value, String& err) {
    Settings t = s;   // validate on a copy, commit only if everything is valid
    bool ok;
    if      (key == "device_id")       ok = toText(value, t.deviceId, sizeof t.deviceId, false);
    else if (key == "apn")             ok = toText(value, t.apn, sizeof t.apn, true);
    else if (key == "mqtt_host")       ok = toText(value, t.mqttHost, sizeof t.mqttHost, false);
    else if (key == "mqtt_user")       ok = toText(value, t.mqttUser, sizeof t.mqttUser, true);
    else if (key == "mqtt_pass")       ok = toText(value, t.mqttPass, sizeof t.mqttPass, true);
    else if (key == "mqtt_tls")        ok = toBool(value, t.mqttTls);
    else if (key == "allow_roaming")   ok = toBool(value, t.allowRoaming);
    else if (key == "modem_always_on") ok = toBool(value, t.modemAlwaysOn);
    else if (key == "simulate")        ok = toBool(value, t.simulate);
    else if (key == "mqtt_port") {
        uint32_t v; ok = toUint(value, 1, 65535, v); if (ok) t.mqttPort = (uint16_t)v;
    }
    else if (key == "sample_s")        ok = toUint(value, 5, 3600, t.sampleS);
    else if (key == "upload_s")        ok = toUint(value, 30, 86400, t.uploadS);
    else if (key == "live_sample_s")   ok = toUint(value, 5, 600, t.liveSampleS);
    else if (key == "cycle_hold_s")    ok = toUint(value, 60, 86400, t.cycleHoldS);
    else if (key == "range_min")       ok = toFloat(value, -50.0f, 1800.0f, t.rangeMinC);
    else if (key == "range_max")       ok = toFloat(value, -50.0f, 1800.0f, t.rangeMaxC);
    else if (key == "shunt_ohm")       ok = toFloat(value, 50.0f, 250.0f, t.shuntOhms);
    else if (key == "cycle_start_c")   ok = toFloat(value, 20.0f, 1500.0f, t.cycleStartC);
    else if (key == "cycle_end_c")     ok = toFloat(value, 0.0f, 1500.0f, t.cycleEndC);
    else if (key == "batt_low_v")      ok = toFloat(value, 3.0f, 4.0f, t.battLowV);
    else { err = "unknown key '" + key + "'"; return false; }

    if (!ok) { err = "invalid value for " + key; return false; }
    if (t.rangeMaxC <= t.rangeMinC) { err = "range_max must be above range_min"; return false; }
    if (t.cycleEndC >= t.cycleStartC) { err = "cycle_end_c must be below cycle_start_c"; return false; }
    s = t;
    return true;
}

void toJson(const Settings& s, JsonObject o) {
    o["device_id"] = s.deviceId;
    o["apn"] = s.apn;
    o["mqtt_host"] = s.mqttHost;
    o["mqtt_port"] = s.mqttPort;
    o["mqtt_tls"] = s.mqttTls;
    o["mqtt_user"] = s.mqttUser;
    o["mqtt_pass_set"] = s.mqttPass[0] != 0;
    o["sample_s"] = s.sampleS;
    o["upload_s"] = s.uploadS;
    o["live_sample_s"] = s.liveSampleS;
    o["range_min"] = s.rangeMinC;
    o["range_max"] = s.rangeMaxC;
    o["shunt_ohm"] = s.shuntOhms;
    o["cycle_start_c"] = s.cycleStartC;
    o["cycle_end_c"] = s.cycleEndC;
    o["cycle_hold_s"] = s.cycleHoldS;
    o["batt_low_v"] = s.battLowV;
    o["allow_roaming"] = s.allowRoaming;
    o["modem_always_on"] = s.modemAlwaysOn;
    o["simulate"] = s.simulate;
}

}  // namespace settings
