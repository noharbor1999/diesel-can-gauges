#pragma once
#include <stdint.h>

#define RAW_MAX 44   // largest Mode 01 PID is 41 data bytes

struct Resp {
  uint8_t ecu;          // 0..7  => 0x7E8+ecu
  uint8_t len;          // payload length (starts 41 <pid> or 7F 01 <nrc>)
  uint8_t data[64];
};

struct Rec {
  uint8_t sweep, pid, ecu;   // ecu 0..7, 0xFF = no response
  uint8_t kind;              // 0 positive, 1 negative, 2 no response
  uint8_t len;               // full payload length (41 pid ...)
  uint8_t baro;
  uint8_t raw[RAW_MAX + 2];
};
