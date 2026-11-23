// g3_packet.h - SHARED between hub/ and display/. Keep both copies IDENTICAL.
// (Arduino only compiles files inside the sketch folder, so each sketch has a copy.
//  tools/check_shared.sh compares them.)
//
// ESP-NOW broadcast from the under-dash hub to the 3 gauge displays, ~20 Hz.
// The hub owns ALL state (values, peaks, alarms, focus, pages, settings,
// brightness, mute). Displays are renderers + buzzer + their own IMU.
#pragma once
#include <stdint.h>
#include <string.h>
#include <stddef.h>

#define G3_MAGIC        0x4733      // "G3"
#define G3_VERSION      2           // v2 (Nov 22 2026): VGT removed, BRAKE HP added. Reflash hub AND all 3 displays
#define G3_NET_ID       0x5A        // change on hub AND displays if another pod is near
#define G3_WIFI_CHANNEL 1           // ESP-NOW channel, same on all boards
#define G3_TX_PERIOD_MS 50          // 20 Hz
#define G3_LINK_TIMEOUT_MS 1000     // display shows NO SIGNAL after this

// ---- live values (index into G3Packet::val[] / peak[]) ----
enum ValId : uint8_t {
  V_EGT = 0,   // deg F   (MAX31855, cyl-6 pre-turbo)
  V_BOOST,     // psi gauge (MAP - baro)
  V_EBP,       // psi gauge (exhaust back pressure - baro)
  V_FRP_ACT,   // psi     fuel rail pressure actual
  V_FRP_CMD,   // psi     fuel rail pressure commanded
  V_LOAD,      // %       calculated engine load
  V_PEDAL,     // %       accelerator pedal
  V_IAT,       // deg F   intake air temp
  V_BATT,      // V       control module voltage
  V_RPM,       // rpm
  V_BRAKE_HP,  // hp      exhaust-brake power, CAN-C 0x30A byte 7 x 1.32 (same 0-230 scale as the dash). >0 = brake working
  V_COUNT
};

enum Focus : uint8_t { FOCUS_NONE = 0, FOCUS_G1, FOCUS_G2, FOCUS_G3, FOCUS_ALL, FOCUS_COUNT };
enum Page  : uint8_t { PAGE_MAIN = 0, PAGE_PEAK, PAGE_SETTINGS, PAGE_COUNT };
enum UtilPage : uint8_t { UP_FRP = 0, UP_LOAD, UP_IAT, UP_BATT, UP_TILT, UP_COUNT };   // VGT page removed (v2)

// ---- flags ----
enum : uint16_t {
  F_TEST_MODE   = 1 << 0,   // any bench override or sweep active
  F_SWEEP       = 1 << 1,
  F_CAN_OK      = 1 << 2,   // ECM answered in the last 2 s
  F_EGT_PROBE   = 1 << 3,   // MAX31855 reads a probe (no open/short fault)
  F_MUTED       = 1 << 4,   // buzzer muted for the current alarm
  F_G2_EBP      = 1 << 5,   // gauge 2 currently shows EBP (else BOOST)
  F_SET_EDIT    = 1 << 6,   // settings item is being edited
  F_EBP_SUPPORTED = 1 << 7,
  F_EB_BCAST    = 1 << 8,   // CAN-C 0x30A (exhaust-brake effort) seen in the last 2 s
};

// ---- alarm / warning bits ----
enum : uint32_t {
  A_EGT_WARN   = 1u << 0, A_EGT_ALARM   = 1u << 1,
  A_BOOST_WARN = 1u << 2, A_BOOST_ALARM = 1u << 3,
  A_EBP_WARN   = 1u << 4, A_EBP_ALARM   = 1u << 5,
  A_FRP_MM     = 1u << 6,   // rail pressure actual lags commanded (RAIL LAG)
  A_FRP_LOW    = 1u << 7,   // rail pressure actual below RAIL LOW with the engine running
  A_BATT_LOW   = 1u << 8,
  A_IAT_WARN   = 1u << 9,
};
// alarms that sound a buzzer, and which gauge buzzes
#define A_BUZZ_G1 (A_EGT_ALARM)
#define A_BUZZ_G2 (A_BOOST_ALARM | A_EBP_ALARM)
#define A_BUZZ_G3 (A_FRP_MM | A_FRP_LOW)
#define A_BUZZ_ALL (A_BUZZ_G1 | A_BUZZ_G2 | A_BUZZ_G3)

// ---- settings (stored in hub NVS, edited with the knob) ----
// ALL DEFAULTS ARE EDITABLE STARTING VALUES, NOT FACTORY TRUCK SPECS.
enum SetId : uint8_t {
  S_EGT_WARN = 0, S_EGT_ALARM,
  S_BOOST_WARN, S_BOOST_ALARM, S_EBP_WARN, S_EBP_ALARM, S_G2_DELAY,
  S_FRP_MM, S_FRP_LOW, S_BATT_LOW, S_IAT_WARN,
  S_COUNT
};
struct SettingDef { const char* name; const char* unit; float def, mn, mx, step; uint8_t gauge; uint8_t decimals; };
static const SettingDef G3_SETTINGS[S_COUNT] = {
  // name           unit    default   min     max    step  gauge dec
  {"EGT WARN",     "\x7f" "F", 1250,   800,   2000,   10,   1, 0},   // starting value
  {"EGT ALARM",    "\x7f" "F", 1400,   900,   2200,   10,   1, 0},   // starting value
  {"BOOST WARN",   "PSI",     35,      10,     60,     1,   2, 0},   // guess, just above 32.3 seen (Nov 22 drive)
  {"BOOST ALARM",  "PSI",     38,      10,     65,     1,   2, 0},   // guess, not a spec
  {"EBP WARN",     "PSI",     65,      10,     80,     1,   2, 0},   // brake plateau 55-60, peak 66 seen
  {"EBP ALARM",    "PSI",     70,      10,     90,     1,   2, 0},   // owner reports, not a factory spec
  {"SWITCH DELAY", "S",       1.0f,    0.2f,   3.0f, 0.1f,  2, 1},   // boost->EBP delay
  {"RAIL LAG",     "PSI",   2500,    500,  10000,  250,   3, 0},   // act below cmd by this for >1 s (spikes seen were <0.5 s)
  {"RAIL LOW",     "PSI",   4000,   1000,  10000,  250,   3, 0},   // act below this, engine running (idle min seen 4,456)
  {"BATT LOW",     "V",     12.0f,   10.0f,  14.0f, 0.1f,  3, 1},
  {"IAT WARN",     "\x7f" "F", 150,     80,    250,     5,   3, 0},
};
#define G3_MISMATCH_MS 1000   // rail lag / rail low must last this long before the alarm

#pragma pack(push, 1)
struct G3Packet {
  uint16_t magic;
  uint8_t  version;
  uint8_t  net_id;
  uint32_t seq;            // +1 every packet
  uint32_t hub_ms;
  uint16_t flags;          // F_*
  uint16_t valid;          // bit n = val[n] is fresh
  uint32_t alarms;         // A_* active
  float    val[V_COUNT];
  float    peak[V_COUNT];  // max since reset (V_BATT holds the MINIMUM)
  uint8_t  focus;          // Focus
  uint8_t  page[3];        // Page per gauge
  uint8_t  util_page;      // UtilPage for gauge 3
  uint8_t  set_idx;        // highlighted item inside the focused gauge's settings list
  uint8_t  brightness;     // 5..100 %
  uint8_t  reset_count[3]; // increments on peak reset per gauge
  uint8_t  imu_zero_count; // increments when user zeroes pitch/roll
  uint8_t  bright_count;   // increments when brightness changes (shows a toast)
  float    set_val[S_COUNT];
  float    edit_val;       // value being edited (valid when F_SET_EDIT)
  uint16_t crc;            // CRC-16/CCITT over all bytes before crc
};
#pragma pack(pop)
static_assert(sizeof(G3Packet) <= 250, "ESP-NOW payload limit");

static inline uint16_t g3_crc16(const uint8_t* d, size_t n) {
  uint16_t c = 0xFFFF;
  while (n--) { c ^= (uint16_t)(*d++) << 8; for (int i = 0; i < 8; i++) c = (c & 0x8000) ? (c << 1) ^ 0x1021 : (c << 1); }
  return c;
}
static inline void g3_seal(G3Packet& p) {
  p.magic = G3_MAGIC; p.version = G3_VERSION; p.net_id = G3_NET_ID;
  p.crc = g3_crc16((const uint8_t*)&p, offsetof(G3Packet, crc));
}
static inline bool g3_check(const uint8_t* d, int len) {
  if (len != (int)sizeof(G3Packet)) return false;
  G3Packet p; memcpy(&p, d, sizeof p);
  return p.magic == G3_MAGIC && p.version == G3_VERSION && p.net_id == G3_NET_ID &&
         p.crc == g3_crc16(d, offsetof(G3Packet, crc));
}
// number of settings items shown on gauge g (1..3) and the SetId of item i
static inline int g3_settings_count(int g) { int n = 0; for (int i = 0; i < S_COUNT; i++) if (G3_SETTINGS[i].gauge == g) n++; return n; }
static inline int g3_settings_id(int g, int item) {
  for (int i = 0; i < S_COUNT; i++) if (G3_SETTINGS[i].gauge == g) { if (item-- == 0) return i; }
  return -1;
}
