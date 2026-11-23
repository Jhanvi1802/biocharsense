#pragma once
#include <Arduino.h>

// Serial log lines: "[  12345] I message" (milliseconds since boot).
#define LOG_LINE(level, fmt, ...) \
    Serial.printf("[%8lu] " level " " fmt "\n", (unsigned long)millis(), ##__VA_ARGS__)
#define LOGI(fmt, ...) LOG_LINE("I", fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) LOG_LINE("W", fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) LOG_LINE("E", fmt, ##__VA_ARGS__)
