// sniff_proto.h - shared packet format for the OBD2 sniff-everything project.
// KEEP THIS FILE IDENTICAL in sniffer_node/ and gateway/ (tools/sync_common.sh copies it).
//
// Node (one per CAN pair, TWAI LISTEN-ONLY) --ESP-NOW--> Gateway (USB to laptop, GVRET to SavvyCAN)
#pragma once
#include <stdint.h>

#define SNIFF_MAGIC          0x53      // 'S'
#define SNIFF_VERSION        1

// Fixed Wi-Fi channel. Nodes and the gateway MUST use the same value.
// Change it here (then reflash ALL boards) if channel 6 is noisy where you are.
#define SNIFF_WIFI_CHANNEL   6

// Node IDs 0..4 map to SavvyCAN/GVRET bus numbers 0..4.
// Bus 5 carries the gateway's own status frames (see gateway.ino).
#define SNIFF_MAX_NODES      5
#define SNIFF_STATUS_BUS     5

// ESP-NOW v2 (ESP-IDF 5.4+, Arduino-ESP32 3.2+) allows 1470-byte packets. v1 allows 250.
// Both ends of THIS project are built with the same core, so v2 is used by default.
#ifndef SNIFF_USE_ESPNOW_V2
#define SNIFF_USE_ESPNOW_V2  1
#endif
#if SNIFF_USE_ESPNOW_V2
#define SNIFF_PAYLOAD_MAX    1470
#else
#define SNIFF_PAYLOAD_MAX    250
#endif

enum : uint8_t {
  PKT_FRAMES = 1,   // node -> gateway: batch of CAN frames
  PKT_STATS  = 2,   // node -> gateway: once per second
  PKT_BEACON = 3,   // gateway -> broadcast: "I am here" every 500 ms
};

// Node states (also used for the node LED blink count)
enum : uint8_t {
  NODE_ST_SEARCHING = 1,  // looking for bus speed / bus silent
  NODE_ST_NO_GATEWAY = 2, // bus OK, gateway not heard yet
  NODE_ST_OK = 3,         // streaming
  NODE_ST_DROPPING = 4,   // frames dropped in the last 5 s
  NODE_ST_BUS_ERRORS = 5, // bus errors (wrong speed / wiring)
  NODE_ST_FAKE = 6,       // fake-bus bench mode, streaming
};

#pragma pack(push, 1)
struct PktHdr {
  uint8_t  magic;      // SNIFF_MAGIC
  uint8_t  version;    // SNIFF_VERSION
  uint8_t  type;       // PKT_*
  uint8_t  node;       // node id 0..4
  uint16_t seq;        // +1 every packet (frames AND stats) -> gateway counts packet gaps
  uint8_t  count;      // number of frame records (PKT_FRAMES)
  uint8_t  flags;      // bit0 = fake-bus data
  uint32_t t_base_us;  // node micros() of the first frame in this packet
  uint32_t first_idx;  // running index of the first frame -> gateway counts LOST FRAMES exactly
};
// After PktHdr, PKT_FRAMES carries `count` variable-length records:
//   uint16_t dt_us   (frame time - t_base_us, packet is flushed before this overflows)
//   uint32_t id      (bit31 = extended 29-bit id, bit30 = remote frame)
//   uint8_t  dlc     (0..8)
//   uint8_t  data[dlc]
#define SNIFF_REC_HDR  7
#define SNIFF_ID_EXT   0x80000000UL
#define SNIFF_ID_RTR   0x40000000UL

struct PktStats {
  PktHdr   h;
  uint32_t baud;        // 0 = not found yet
  uint8_t  state;       // NODE_ST_*
  uint8_t  auto_baud;   // 1 = auto, 0 = manual override
  uint8_t  tec, rec;    // TWAI error counters (stay 0 in listen-only unless errors)
  uint32_t rx_frames;   // frames received from the bus (total)
  uint32_t bus_errors;  // TWAI bus error count (total)
  uint32_t rx_missed;   // TWAI driver rx queue full (total)
  uint32_t rx_overrun;  // TWAI hardware FIFO overrun (total)
  uint32_t q_drops;     // node internal queue full (total)
  uint32_t tx_fail;     // ESP-NOW packets that failed / were not sent (total)
  uint32_t tx_ok;       // ESP-NOW packets acknowledged (total)
  uint32_t uptime_s;
  uint16_t fps;         // bus frames per second, last second
  uint16_t reserved;
};

struct PktBeacon {
  uint8_t  magic, version, type, channel;
  uint32_t gw_uptime_ms;
};
#pragma pack(pop)
