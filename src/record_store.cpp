#include "record_store.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "log.h"

namespace {
const char* PATH = "/records.bin";
const char* NS = "bs_queue";
}

bool RecordStore::begin() {
    if (!LittleFS.begin(true)) {
        LOGE("LittleFS mount failed");
        return false;
    }
    Preferences p;
    p.begin(NS, true);
    uint32_t cap = p.getUInt("cap", 0);
    _ring.capacity = RECORD_CAPACITY;
    _ring.head = p.getUInt("head", 0);
    _ring.tail = p.getUInt("tail", 0);
    _ring.dropped = p.getUInt("drop", 0);
    p.end();

    bool fresh = cap != RECORD_CAPACITY || _ring.head > _ring.tail || _ring.count() > _ring.capacity;
    if (fresh) {
        _ring.head = _ring.tail = _ring.dropped = 0;
        saveMeta();
    }
    _ok = openFile(fresh);
    if (_ok && !fresh) recoverTail();
    LOGI("store: %s, %u pending, next seq %u", _ok ? "ok" : "FAILED",
         (unsigned)pending(), (unsigned)_ring.tail);
    return _ok;
}

bool RecordStore::openFile(bool recreate) {
    const size_t size = (size_t)RECORD_CAPACITY * sizeof(Record);
    if (recreate || !LittleFS.exists(PATH) || LittleFS.open(PATH, "r").size() != size) {
        LOGI("store: creating %u-byte buffer", (unsigned)size);
        File f = LittleFS.open(PATH, "w");
        if (!f) return false;
        uint8_t zero[512] = {0};
        for (size_t done = 0; done < size; done += sizeof zero) {
            if (f.write(zero, sizeof zero) != sizeof zero) { f.close(); return false; }
            esp_task_wdt_reset();
        }
        f.close();
    }
    _file = LittleFS.open(PATH, "r+");
    return (bool)_file;
}

// The queue position is saved only every META_SAVE_MS to spare the NVS flash,
// so after a power cut the last few readings are newer than the saved tail.
// They are found by reading forward from it. A head that is a little behind
// only means a few readings are sent twice; the server drops duplicates.
void RecordStore::recoverTail() {
    uint32_t found = 0;
    Record r;
    while (found < _ring.capacity && read(_ring.tail, r, true)) {
        _ring.tail++;
        found++;
        if (_ring.count() > _ring.capacity) { _ring.head++; _ring.dropped++; }
    }
    if (found) {
        LOGI("store: recovered %u readings written after the last save", (unsigned)found);
        saveMeta();
    }
}

void RecordStore::saveMetaSoon() {
    if (millis() - _lastSaveMs >= META_SAVE_MS) saveMeta();
}

void RecordStore::sync() {
    saveMeta();
}

void RecordStore::saveMeta() {
    _lastSaveMs = millis();
    Preferences p;
    p.begin(NS, false);
    p.putUInt("cap", RECORD_CAPACITY);
    p.putUInt("head", _ring.head);
    p.putUInt("tail", _ring.tail);
    p.putUInt("drop", _ring.dropped);
    p.end();
}

uint32_t RecordStore::append(Record& r) {
    r.seq = _ring.append();
    r.magic = RECORD_MAGIC;
    if (_ok && _file.seek((size_t)_ring.slotOf(r.seq) * sizeof(Record))) {
        _file.write((const uint8_t*)&r, sizeof r);
        _file.flush();
    } else {
        LOGE("store: write failed for seq %u", (unsigned)r.seq);
    }
    saveMetaSoon();
    return r.seq;
}

bool RecordStore::read(uint32_t seq, Record& r, bool beyondTail) {
    if (!_ok || seq < _ring.oldest() || (!beyondTail && seq >= _ring.tail)) return false;
    if (!_file.seek((size_t)_ring.slotOf(seq) * sizeof(Record))) return false;
    if (_file.read((uint8_t*)&r, sizeof r) != sizeof r) return false;
    return r.seq == seq && r.magic == RECORD_MAGIC;
}

void RecordStore::ackThrough(uint32_t seq) {
    _ring.ackThrough(seq);
    saveMetaSoon();
}

uint32_t RecordStore::rewind(uint32_t fromSeq) {
    uint32_t from = _ring.rewind(fromSeq);
    saveMeta();
    return from;
}

void RecordStore::clear() {
    _ring.head = _ring.tail;
    saveMeta();
}
