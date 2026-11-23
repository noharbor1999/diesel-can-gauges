// Minimal Arduino API for host rendering/testing of egt_gauge.ino (not used on the ESP32)
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>
using std::min; using std::max;
#ifndef ARDUINO
#define ARDUINO 10813
#endif
#define PROGMEM
#define IRAM_ATTR
#define HIGH 1
#define LOW 0
#define INPUT_PULLUP 2
#define OUTPUT 3
#define DEG_TO_RAD 0.017453292519943295
#define radians(d) ((d) * DEG_TO_RAD)
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
typedef bool boolean;
typedef uint8_t byte;
extern uint32_t g_ms;
extern int g_pin[64];
inline uint32_t millis() { return g_ms; }
inline int digitalRead(int p) { return g_pin[p]; }
inline void digitalWrite(int p, int v) { g_pin[p] = v; }
inline void pinMode(int, int) {}
inline void noInterrupts() {}
inline void interrupts() {}
#include "Print.h"
