#pragma once
#include "Arduino.h"
#include "Adafruit_GFX.h"
#include <map>
#include <string>
#include <vector>
// In-memory stand-in for ESP32 Preferences (NVS). Counts writes so tests can check flash wear.
class Preferences {
public:
  std::map<std::string, std::vector<uint8_t>> m; int writes = 0;
  bool begin(const char *, bool) { return true; }
  bool isKey(const char *k) { return m.count(k) > 0; }
  bool remove(const char *k) { writes++; return m.erase(k) > 0; }
  size_t putBytes(const char *k, const void *v, size_t n) { writes++; m[k].assign((const uint8_t *)v, (const uint8_t *)v + n); return n; }
  size_t getBytes(const char *k, void *v, size_t n) { if (!m.count(k)) return 0; auto &x = m[k]; if (x.size() > n) return 0; memcpy(v, x.data(), x.size()); return x.size(); }
  template <class T> T getT(const char *k, T d) { if (!m.count(k) || m[k].size() != sizeof(T)) return d; T v; memcpy(&v, m[k].data(), sizeof v); return v; }
  int getInt(const char *k, int d) { return getT<int32_t>(k, d); }
  size_t putInt(const char *k, int v) { int32_t x = v; return putBytes(k, &x, 4); }
  uint8_t getUChar(const char *k, uint8_t d) { return getT<uint8_t>(k, d); }
  size_t putUChar(const char *k, uint8_t v) { return putBytes(k, &v, 1); }
  uint16_t getUShort(const char *k, uint16_t d) { return getT<uint16_t>(k, d); }
  size_t putUShort(const char *k, uint16_t v) { return putBytes(k, &v, 2); }
};
