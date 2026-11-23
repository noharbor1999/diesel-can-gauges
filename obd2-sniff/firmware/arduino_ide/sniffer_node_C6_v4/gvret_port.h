// gvret_port.h - minimal GVRET device protocol (the serial protocol SavvyCAN speaks).
// KEEP THIS FILE IDENTICAL in sniffer_node/ and gateway/.
//
// RECEIVE ONLY. If SavvyCAN asks to SEND a frame (cmd 0x00) or change bus setup (cmd 0x05),
// the request is read and THROWN AWAY. Nothing in this project ever transmits on the truck bus.
//
// Protocol notes taken from SavvyCAN connections/gvretserial.cpp (master, read Nov 2026):
//   host sends E7 E7 -> binary mode; commands are F1 <cmd> ...
//   frame to host: F1 00 <ts u32 LE> <id u32 LE, bit31=ext> <(dlc&0xF)|(bus<<4)> <data> <0x00>
#pragma once
#include <Arduino.h>

class GvretPort {
 public:
  Stream*  port = nullptr;
  bool     binary = false;      // true after host sent 0xE7
  uint8_t  numBuses = 1;
  uint32_t bus0Speed = 500000, bus1Speed = 0;
  uint32_t lastHostByteMs = 0;

  void begin(Stream* p, uint8_t nb) { port = p; numBuses = nb; }

  // Feed every byte the host sends. Replies are written immediately (caller holds any write lock).
  void feed(uint8_t c) {
    lastHostByteMs = millis();
    if (skip > 0) { skip--; return; }
    switch (st) {
      case 0:
        if (c == 0xE7) { binary = true; }
        else if (c == 0xF1) { st = 1; }
        break;
      case 1: command(c); break;
      case 2:  // host BUILD_CAN_FRAME header: id(4) bus(1) len(1) -> we only need len
        hdrCount++;
        if (hdrCount == 6) { skip = (c & 0x0F); st = 0; }
        break;
      default: st = 0; break;
    }
  }

  void sendFrame(uint32_t ts, uint32_t id, bool ext, uint8_t bus, uint8_t dlc, const uint8_t* d) {
    if (!port || !binary) return;
    uint8_t b[24];
    if (dlc > 8) dlc = 8;
    if (ext) id |= 0x80000000UL; else id &= 0x7FF;
    b[0] = 0xF1; b[1] = 0x00;
    put32(&b[2], ts); put32(&b[6], id);
    b[10] = (dlc & 0x0F) | ((bus & 0x0F) << 4);
    memcpy(&b[11], d, dlc);
    b[11 + dlc] = 0;
    port->write(b, 12 + dlc);
  }

 private:
  uint8_t st = 0, hdrCount = 0, skip = 0;

  static void put32(uint8_t* p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

  void reply(const uint8_t* b, size_t n) { if (port) port->write(b, n); }

  void command(uint8_t c) {
    st = 0;
    switch (c) {
      case 0x00: hdrCount = 0; st = 2; break;            // host wants to TRANSMIT -> ignored
      case 0x01: { uint8_t b[6] = {0xF1, 0x01}; put32(&b[2], (uint32_t)micros()); reply(b, 6); } break;
      case 0x02: { uint8_t b[4] = {0xF1, 0x02, 0, 0}; reply(b, 4); } break;
      case 0x03: break;                                   // analog inputs: not supported, no reply
      case 0x04: skip = 1; break;                         // set digital out: ignored
      case 0x05: skip = 8; break;                         // SETUP_CANBUS: ignored, we stay listen-only
      case 0x06: {
        uint8_t b[12] = {0xF1, 0x06};
        b[2] = 0x11; put32(&b[3], bus0Speed);             // enabled + listen-only
        b[7] = (numBuses > 1) ? 0x11 : 0x00; put32(&b[8], bus1Speed);
        reply(b, 12);
      } break;
      case 0x07: { uint8_t b[8] = {0xF1, 0x07, 0x2A, 0x01, 0x20, 0, 0, 0}; reply(b, 8); } break;
      case 0x08: skip = 1; break;
      case 0x09: { uint8_t b[4] = {0xF1, 0x09, 0xDE, 0xAD}; reply(b, 4); } break;
      case 0x0A: skip = 1; break;
      case 0x0B: hdrCount = 0; st = 2; break;            // echo frame: ignored
      case 0x0C: { uint8_t b[3] = {0xF1, 0x0C, numBuses}; reply(b, 3); } break;
      case 0x0D: { uint8_t b[17] = {0xF1, 0x0D}; reply(b, 17); } break;   // no SWCAN/LIN
      case 0x0E: skip = 12; break;
      default: break;
    }
  }
};
