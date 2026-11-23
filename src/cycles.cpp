#include "cycles.h"
#include <Preferences.h>
#include "log.h"

namespace {
const char* NS = "bs_cycle";

void copyBatch(char* dst, const char* src) {
    strncpy(dst, src ? src : "", 31);
    dst[31] = 0;
}
}

void CycleManager::begin(const Settings& s, EventQueue& events) {
    _events = &events;
    _deviceId = s.deviceId;
    Preferences p;
    p.begin(NS, true);
    _counter = p.getUInt("ctr", 0);
    if (p.getBytesLength("st") == sizeof _st) p.getBytes("st", &_st, sizeof _st);
    p.end();
    configure(s);
    if (_st.active) {
        _det.forceStart();
        LOGI("cycle %s resumed after reboot", cycleId(_st.number).c_str());
    }
}

void CycleManager::configure(const Settings& s) {
    _det.startC = s.cycleStartC;
    _det.endC = s.cycleEndC;
    _det.endHoldS = s.cycleHoldS;
    _det.confirmN = DEF_CYCLE_CONFIRM;
}

String CycleManager::cycleId(uint32_t number) const {
    char buf[48];
    snprintf(buf, sizeof buf, "%s-%04u", _deviceId.c_str(), (unsigned)number);
    return String(buf);
}

void CycleManager::before(float tempC, bool valid, uint32_t monoS, uint32_t nowTs) {
    switch (_det.update(tempC, valid, monoS)) {
        case core::CycleDetector::STARTED:
            if (!_st.active) open("", false, nowTs);
            break;
        case core::CycleDetector::ENDED:
            _endRequested = true;     // close after this reading is stored
            break;
        default:
            break;
    }
}

void CycleManager::after(const Record& r, uint32_t nowTs) {
    if (_st.active && r.cycleNo == _st.number) {
        if (_st.readings == 0) _st.firstSeq = r.seq;
        _st.lastSeq = r.seq;
        _st.readings++;
        if (r.tempDeci != INT16_MIN && r.tempDeci > _st.peakDeci) _st.peakDeci = r.tempDeci;
        // The running totals are saved at most every META_SAVE_MS to spare the
        // flash; start and end are always saved at once.
        if (millis() - _lastPersistMs >= META_SAVE_MS) persist();
    }
    if (_endRequested) {
        _endRequested = false;
        if (_st.active) close(nowTs, "cooled_down");
    }
}

bool CycleManager::start(const char* batchId, uint32_t nowTs, String& err) {
    const char* b = batchId ? batchId : "";
    if (strlen(b) > 31) { err = "batch_id longer than 31 characters"; return false; }
    if (_st.active) {
        // Kiln already lit and auto-detected: attach the batch to this cycle.
        if (_st.batchId[0] == 0 || strcmp(_st.batchId, b) == 0) {
            if (_st.batchId[0] == 0 && b[0]) {
                copyBatch(_st.batchId, b);
                persist();
                JsonDocument d = _events->make("cycle_update");
                d["cycle_id"] = cycleId(_st.number);
                d["batch_id"] = _st.batchId;
                _events->push(d);
            }
            return true;
        }
        err = String("cycle already running for batch ") + _st.batchId + "; end it first";
        return false;
    }
    open(b, true, nowTs);
    _det.forceStart();
    return true;
}

bool CycleManager::end(uint32_t nowTs, const char* reason, String& err) {
    if (!_st.active) { err = "no active cycle"; return false; }
    close(nowTs, reason);
    _det.forceEnd();
    return true;
}

void CycleManager::open(const char* batchId, bool byCommand, uint32_t nowTs) {
    _counter++;
    _st = State{};
    _st.active = 1;
    _st.byCommand = byCommand ? 1 : 0;
    _st.number = _counter;
    copyBatch(_st.batchId, batchId);
    _st.startTs = nowTs;
    _st.peakDeci = INT16_MIN;
    persist();

    JsonDocument d = _events->make("cycle_start");
    d["cycle_id"] = cycleId(_st.number);
    d["batch_id"] = _st.batchId;
    d["started_by"] = byCommand ? "command" : "auto";
    _events->push(d);
    LOGI("cycle %s started (%s) batch '%s'", cycleId(_st.number).c_str(),
         byCommand ? "command" : "auto", _st.batchId);
}

void CycleManager::close(uint32_t nowTs, const char* reason) {
    JsonDocument d = _events->make("cycle_end");
    d["cycle_id"] = cycleId(_st.number);
    d["batch_id"] = _st.batchId;
    d["reason"] = reason;
    if (_st.startTs) d["start_ts"] = isoTime(_st.startTs);
    d["readings"] = _st.readings;
    d["first_seq"] = _st.firstSeq;
    d["last_seq"] = _st.lastSeq;
    _events->push(d);
    LOGI("cycle %s ended (%s), %u readings", cycleId(_st.number).c_str(), reason,
         (unsigned)_st.readings);
    _st.active = 0;
    persist();
}

void CycleManager::persist() {
    _lastPersistMs = millis();
    Preferences p;
    p.begin(NS, false);
    p.putUInt("ctr", _counter);
    p.putBytes("st", &_st, sizeof _st);
    p.end();
}

void CycleManager::reset() {
    _st = State{};
    _det.forceEnd();
    persist();
}
