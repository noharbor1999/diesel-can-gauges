#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
class __FlashStringHelper;
class String {                       // just enough for Adafruit_GFX's String overloads
  const char *p;
public:
  String(const char *s = "") : p(s) {}
  unsigned int length() const { return (unsigned)strlen(p); }
  const char *c_str() const { return p; }
};
class Print {
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t *b, size_t n) { size_t r = 0; while (n--) r += write(*b++); return r; }
  size_t print(const char *s) { return write((const uint8_t *)s, strlen(s)); }
  size_t print(int v) { char b[16]; snprintf(b, 16, "%d", v); return print(b); }
};
