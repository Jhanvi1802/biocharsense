#pragma once
#include <Arduino.h>
#include <FS.h>
#include "core.h"

// One stored reading. Fixed size so slot N sits at byte N * 32 in the file.
struct __attribute__((packed)) Record {
    uint32_t seq;
    uint32_t ts;           // UTC epoch; 0 = clock not known when taken
    uint32_t monoS;        // seconds since boot when taken
    uint32_t cycleNo;      // 0 = no burn cycle active
    uint16_t bootId;
    int16_t  tempDeci;     // deg C x 10; INT16_MIN = no valid temperature
    uint16_t loopCentiMa;  // mA x 100
    uint16_t battMv;
    uint8_t  fault;        // core::Fault
    uint8_t  timeSrc;      // TimeSource
    uint8_t  magic;        // RECORD_MAGIC once written
    uint8_t  reserved[5];
};
static_assert(sizeof(Record) == 32, "Record must stay 32 bytes");

// Store-and-forward queue in LittleFS. Readings stay in flash (surviving
// reboots and power loss) until the broker confirms them; if the buffer
// fills during a long outage, the oldest unsent reading is overwritten.
class RecordStore {
public:
    bool begin();
    uint32_t append(Record& r);              // assigns and returns r.seq
    bool read(uint32_t seq, Record& r, bool beyondTail = false);
    uint32_t pending() const { return _ring.count(); }
    uint32_t headSeq() const { return _ring.head; }
    uint32_t tailSeq() const { return _ring.tail; }
    uint32_t dropped() const { return _ring.dropped; }
    void ackThrough(uint32_t seq);           // broker confirmed up to seq
    uint32_t rewind(uint32_t fromSeq);       // send again from fromSeq
    void clear();
    void sync();                             // save the queue position now (before sleep / reboot)

private:
    core::RingSeq _ring;
    fs::File _file;
    bool _ok = false;
    uint32_t _lastSaveMs = 0;
    bool openFile(bool recreate);
    void saveMeta();
    void saveMetaSoon();
    void recoverTail();
};
