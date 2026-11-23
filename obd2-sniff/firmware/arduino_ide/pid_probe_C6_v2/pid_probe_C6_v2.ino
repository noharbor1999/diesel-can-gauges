// pid_probe_C6 v2 — 2017 Ram 2500/3500 6.7 Cummins, CAN-C 500 kbit/s (OBD pins 6/14)
//
// ONE-SHOT, READ-ONLY sweep of every standard OBD-II Mode 01 PID the truck
// says it supports. Prints to USB Serial at 115200, then stops.
//
//   *** BEST DATA: ENGINE RUNNING. Key-on/engine-off gives defaults/zeros. ***
//
// What it does
//   1. Starts TWAI (NORMAL mode, 500k), waits ~3 s, then sends 01 00 every
//      500 ms until any ECU answers (max ~15 s) so the bus/ECM can wake up.
//   2. Discovers support bitmaps 00/20/40/60/80/A0/C0 per ECU, in a chain.
//   3. Queries every supported PID (bitmap PIDs skipped). 3 tries x 150 ms,
//      ~40 ms gap. 0x7DF functional first, then physical (0x7E0+n) fallback.
//      Accepts replies from 0x7E8..0x7EF and prints which ECU answered.
//   4. Decodes per SAE J1979 (Wikipedia "OBD-II PIDs" + DashLogic J1979
//      listing). Unknown layouts print raw bytes + "raw only".
//      "(check)" = formula from one secondary source, not verified on this truck.
//   5. Runs the sweep 3 times ~2 s apart, prints a summary table and a CSV
//      block, then "PROBE COMPLETE" and idles.
//
// Services used: 0x01 only. No Mode 22, no writes, no Mode 04 (clear codes),
// no session changes. ISO-TP flow control (30 00 00) is sent only to read a
// multi-frame reply; that is part of reading, not a write.
//
// Wiring (same as v1): WCMCU-230 (SN65HVD230) CTX -> XIAO D3 (GPIO21),
// CRX -> D4 (GPIO22), 3V3, GND to OBD pin 5. No 120 ohm terminator.
// Board: Seeed XIAO ESP32-C6, USB CDC on boot enabled.

#include <Arduino.h>
#include "driver/twai.h"
#include <string.h>
#include <stdarg.h>
#include "pid_types.h"   // structs in a header so Arduino prototypes see them

#define PIN_CAN_TX      21          // D3 -> SN65HVD230 CTX
#define PIN_CAN_RX      22          // D4 <- SN65HVD230 CRX
#define PIN_LED         15
#define LED_ACTIVE_LOW  1

#define CAN_REQ_FUNC    0x7DFul     // ISO 15765-4 functional broadcast
#define CAN_REQ_PHYS0   0x7E0ul     // physical request base (ECU n = 0x7E0+n)
#define CAN_RESP0       0x7E8ul     // physical response base (ECU n = 0x7E8+n)
#define N_ECU           8

#define WAKE_DELAY_MS   3000
#define WAKE_PERIOD_MS  500
#define WAKE_MAX_MS     15000
#define REQ_TIMEOUT_MS  150         // per try
#define EXTRA_LISTEN_MS 40          // after the first reply, wait for other ECUs
#define TRIES           3
#define GAP_MS          40          // between requests
#define SWEEPS          3
#define SWEEP_GAP_MS    2000

#define MAX_REC         420

// ---------------------------------------------------------------- utils
static void led(bool on) { digitalWrite(PIN_LED, LED_ACTIVE_LOW ? !on : on); }
static void blink(int n) { for (int i = 0; i < n; i++) { led(true); delay(80); led(false); delay(80); } }
static uint16_t u16(const uint8_t *d, int i) { return ((uint16_t)d[i] << 8) | d[i + 1]; }
static uint32_t u32(const uint8_t *d, int i) {
  return ((uint32_t)d[i] << 24) | ((uint32_t)d[i + 1] << 16) | ((uint32_t)d[i + 2] << 8) | d[i + 3];
}
static float cToF(float c) { return c * 1.8f + 32.0f; }
static const float KPA_PSI = 0.1450377f;

static void printHex2(uint8_t b) { if (b < 16) Serial.print('0'); Serial.print(b, HEX); }

// ---------------------------------------------------------------- CAN
static bool canSend(uint32_t id, const uint8_t data[8]) {
  twai_message_t m;
  memset(&m, 0, sizeof(m));
  m.identifier = id;
  m.data_length_code = 8;
  memcpy(m.data, data, 8);
  return twai_transmit(&m, pdMS_TO_TICKS(50)) == ESP_OK;
}

static bool startCan() {
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
      (gpio_num_t)PIN_CAN_TX, (gpio_num_t)PIN_CAN_RX, TWAI_MODE_NORMAL);
  g.tx_queue_len = 8;
  g.rx_queue_len = 32;
  g.alerts_enabled = TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED;
  twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
  // Hardware filter: accept only standard IDs 0x7E8..0x7EF (diagnostic replies).
  // Single-filter std layout: ID in bits 31..21; mask bit 1 = don't care.
  twai_filter_config_t f;
  f.acceptance_code = (uint32_t)CAN_RESP0 << 21;
  f.acceptance_mask = ~((uint32_t)0x7F8 << 21);
  f.single_filter = true;
  if (twai_driver_install(&g, &t, &f) != ESP_OK) return false;
  if (twai_start() != ESP_OK) return false;
  return true;
}

// Recover from bus-off if the controller got there (e.g. CTX miswired).
static void busHealth() {
  twai_status_info_t s;
  if (twai_get_status_info(&s) != ESP_OK) return;
  if (s.state == TWAI_STATE_BUS_OFF) {
    Serial.println(F("  !! TWAI bus-off, recovering..."));
    twai_initiate_recovery();
    uint32_t t0 = millis();
    while (millis() - t0 < 1500) {
      if (twai_get_status_info(&s) == ESP_OK && s.state == TWAI_STATE_STOPPED) break;
      delay(10);
    }
    twai_start();
  } else if (s.state == TWAI_STATE_STOPPED) {
    twai_start();
  }
}

// ---------------------------------------------------------------- ISO-TP, multi-ECU

struct Slot {
  bool active, done;
  uint16_t expected, gotn;
  uint8_t nextSn;
  uint8_t buf[64];
};

static bool payloadMatches(const uint8_t *p, uint16_t n, uint8_t pid) {
  if (n >= 2 && p[0] == 0x41 && p[1] == pid) return true;
  if (n >= 3 && p[0] == 0x7F && p[1] == 0x01) return true;
  return false;
}

// Send "01 pid" to dest and collect complete replies from 0x7E8..0x7EF.
// Functional (0x7DF): keeps listening EXTRA_LISTEN_MS after the first reply.
// Physical: returns on the first reply from the addressed ECU.
static int transact(uint32_t dest, uint8_t pid, uint32_t timeoutMs, Resp *out, int maxOut) {
  static Slot sl[N_ECU];
  memset(sl, 0, sizeof(sl));
  busHealth();
  twai_message_t junk;
  while (twai_receive(&junk, 0) == ESP_OK) {}    // drop stale replies

  uint8_t fr[8] = {0x02, 0x01, pid, 0, 0, 0, 0, 0};
  if (!canSend(dest, fr)) return -2;

  bool functional = (dest == CAN_REQ_FUNC);
  int physEcu = functional ? -1 : (int)(dest - CAN_REQ_PHYS0);
  int nout = 0;
  uint32_t now = millis();
  uint32_t deadline = now + timeoutMs;

  while ((int32_t)(deadline - millis()) > 0) {
    twai_message_t m;
    if (twai_receive(&m, pdMS_TO_TICKS(5)) != ESP_OK) continue;
    if (m.extd || m.rtr || m.data_length_code < 1) continue;
    if (m.identifier < CAN_RESP0 || m.identifier > CAN_RESP0 + 7) continue;
    int e = (int)(m.identifier - CAN_RESP0);
    if (physEcu >= 0 && e != physEcu) continue;
    Slot &s = sl[e];
    if (s.done) continue;
    uint8_t pci = m.data[0] >> 4;

    if (pci == 0x0) {                               // Single Frame
      uint8_t n = m.data[0] & 0x0F;
      if (n == 0 || n > 7) continue;
      if (!payloadMatches(m.data + 1, n, pid)) continue;   // stale / other PID
      if (m.data[1] == 0x7F && n >= 3 && m.data[3] == 0x78) {   // response pending
        deadline = millis() + 500;
        continue;
      }
      memcpy(s.buf, m.data + 1, n);
      s.gotn = n; s.expected = n; s.done = true;
    } else if (pci == 0x1) {                        // First Frame
      uint16_t exp = ((uint16_t)(m.data[0] & 0x0F) << 8) | m.data[1];
      if (exp < 8 || !payloadMatches(m.data + 2, 6, pid)) continue;
      s.active = true; s.expected = exp; s.nextSn = 1;
      memcpy(s.buf, m.data + 2, 6);
      s.gotn = 6;
      uint8_t fc[8] = {0x30, 0x00, 0x00, 0, 0, 0, 0, 0};   // CTS, BS=0, STmin=0
      canSend(CAN_REQ_PHYS0 + e, fc);              // FC to that ECU's physical ID
      deadline = millis() + timeoutMs;
      continue;
    } else if (pci == 0x2 && s.active) {            // Consecutive Frame
      uint8_t sn = m.data[0] & 0x0F;
      if (sn != s.nextSn) { s.active = false; Serial.println(F("  (ISO-TP sequence error)")); continue; }
      s.nextSn = (s.nextSn + 1) & 0x0F;
      uint16_t remain = s.expected - s.gotn;
      uint16_t chunk = remain < 7 ? remain : 7;
      if (s.gotn + chunk > sizeof(s.buf)) chunk = sizeof(s.buf) - s.gotn;
      memcpy(s.buf + s.gotn, m.data + 1, chunk);
      s.gotn += chunk;
      deadline = millis() + timeoutMs;
      if (s.gotn >= s.expected || s.gotn >= sizeof(s.buf)) { s.active = false; s.done = true; }
      else continue;
    } else {
      continue;
    }

    if (s.done && nout < maxOut) {
      out[nout].ecu = (uint8_t)e;
      out[nout].len = (uint8_t)s.gotn;
      memcpy(out[nout].data, s.buf, s.gotn);
      nout++;
      if (!functional) return nout;
      bool anyActive = false;
      for (int i = 0; i < N_ECU; i++) if (sl[i].active) anyActive = true;
      uint32_t ext = millis() + EXTRA_LISTEN_MS;
      if (!anyActive && (int32_t)(deadline - ext) > 0) deadline = ext;
    }
  }
  return nout;
}

// ---------------------------------------------------------------- support
static uint8_t sup[N_ECU][32];      // bit set per ECU: PID p supported
static uint8_t ecuSeen = 0;          // bit n => 0x7E8+n answered
static bool isSup(int e, uint8_t p) { return sup[e][p >> 3] & (0x80 >> (p & 7)); }
static void setSup(int e, uint8_t p) { sup[e][p >> 3] |= (0x80 >> (p & 7)); }
static bool anySup(uint8_t p) {
  for (int e = 0; e < N_ECU; e++) if ((ecuSeen & (1 << e)) && isSup(e, p)) return true;
  return false;
}

// Query with retries: functional first, then physical to ECUs that claim the PID.
static int query(uint8_t pid, Resp *out, int maxOut, bool bitmapQuery) {
  for (int t = 0; t < TRIES; t++) {
    int n = transact(CAN_REQ_FUNC, pid, REQ_TIMEOUT_MS, out, maxOut);
    if (n > 0) return n;
    delay(GAP_MS);
    for (int e = 0; e < N_ECU; e++) {
      bool want = bitmapQuery ? (e == 0 || (ecuSeen & (1 << e))) : ((ecuSeen & (1 << e)) && isSup(e, pid));
      if (!want) continue;
      n = transact(CAN_REQ_PHYS0 + e, pid, REQ_TIMEOUT_MS, out, maxOut);
      if (n > 0) return n;
      delay(GAP_MS);
    }
  }
  return 0;
}

// ---------------------------------------------------------------- names
struct PidName { uint8_t pid; const char *name; };
static const PidName NAMES[] = {
  {0x01, "Monitor status since DTCs cleared"}, {0x04, "Calculated engine load"},
  {0x05, "Engine coolant temp"}, {0x0C, "Engine RPM"}, {0x0D, "Vehicle speed"},
  {0x0F, "Intake air temp"}, {0x10, "MAF air flow rate"}, {0x1C, "OBD standard"},
  {0x1F, "Run time since engine start"}, {0x21, "Distance with MIL on"},
  {0x2F, "Fuel tank level"}, {0x30, "Warm-ups since codes cleared"},
  {0x31, "Distance since codes cleared"}, {0x33, "Barometric pressure"},
  {0x41, "Monitor status this drive cycle"}, {0x42, "Control module voltage"},
  {0x46, "Ambient air temp"}, {0x49, "Accel pedal position D"},
  {0x4A, "Accel pedal position E"}, {0x4D, "Time run with MIL on"},
  {0x4E, "Time since codes cleared"}, {0x51, "Fuel type"},
  {0x5A, "Relative accel pedal position"}, {0x5D, "Fuel injection timing"},
  {0x5E, "Engine fuel rate"}, {0x61, "Driver's demand torque"},
  {0x62, "Actual engine torque"}, {0x63, "Engine reference torque"},
  {0x64, "Engine percent torque data"}, {0x65, "Aux inputs/outputs"},
  {0x66, "MAF sensor A/B"}, {0x67, "Coolant temp sensors"}, {0x68, "IAT sensors"},
  {0x69, "Commanded EGR / EGR error"}, {0x6A, "Diesel intake air flow control"},
  {0x6B, "EGR temperature"}, {0x6C, "Throttle actuator control"},
  {0x6D, "Fuel pressure control (FRP)"}, {0x6E, "Injection pressure control"},
  {0x6F, "Turbo compressor inlet pressure"}, {0x70, "Boost pressure control"},
  {0x71, "VGT control"}, {0x72, "Wastegate control"}, {0x73, "Exhaust pressure"},
  {0x74, "Turbocharger RPM"}, {0x75, "Turbo A temperatures"}, {0x76, "Turbo B temperatures"},
  {0x77, "Charge air cooler temp (CACT)"}, {0x78, "EGT bank 1"}, {0x79, "EGT bank 2"},
  {0x7A, "DPF bank 1 pressures"}, {0x7B, "DPF bank 2 pressures"}, {0x7C, "DPF temperatures"},
  {0x7D, "NOx NTE control area status"}, {0x7E, "PM NTE control area status"},
  {0x7F, "Engine run time totals"}, {0x81, "AECD run time #1-5"}, {0x82, "AECD run time #6-10"},
  {0x83, "NOx sensor"}, {0x84, "Intake manifold surface temp"}, {0x85, "NOx reagent (DEF) system"},
  {0x86, "PM sensor"}, {0x87, "Intake manifold abs pressure"}, {0x88, "SCR inducement system"},
  {0x89, "AECD run time #11-15"}, {0x8A, "AECD run time #16-20"}, {0x8B, "Diesel aftertreatment status"},
  {0x8C, "O2 sensor (wide range)"}, {0x8D, "Throttle position G"}, {0x8E, "Engine friction torque"},
  {0x8F, "PM sensor bank 1&2"}, {0x90, "WWH-OBD system info"}, {0x91, "WWH-OBD system info"},
  {0x92, "Fuel system control"}, {0x93, "WWH-OBD counters"}, {0x94, "NOx warning/inducement"},
  {0x98, "EGT sensor (5-8) bank 1"}, {0x99, "EGT sensor (5-8) bank 2"}, {0x9A, "Hybrid/EV data"},
  {0x9B, "DEF sensor data"}, {0x9C, "O2 sensor data"}, {0x9D, "Engine fuel rate (g/s)"},
  {0x9E, "Engine exhaust flow rate"}, {0x9F, "Fuel system percentage use"},
  {0xA1, "NOx sensor corrected"}, {0xA2, "Cylinder fuel rate"}, {0xA3, "Evap vapor pressure"},
  {0xA4, "Transmission actual gear"}, {0xA5, "Commanded DEF dosing"}, {0xA6, "Odometer"},
  {0xA7, "NOx sensor 3&4"}, {0xA8, "NOx sensor corrected 3&4"}, {0xA9, "ABS disable switch"},
};
static const char *pidName(uint8_t pid) {
  if ((pid & 0x1F) == 0) return "PIDs supported bitmap";
  for (size_t i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); i++) if (NAMES[i].pid == pid) return NAMES[i].name;
  return "(unnamed PID)";
}

// ---------------------------------------------------------------- decoder
static char *g_o; static size_t g_cap;
static void ap(const char *fmt, ...) {
  size_t l = strlen(g_o);
  if (l + 1 >= g_cap) return;
  va_list a; va_start(a, fmt); vsnprintf(g_o + l, g_cap - l, fmt, a); va_end(a);
}
static void sep() { if (g_o[0]) ap("; "); }
static void apTemp(const char *lbl, float c) { sep(); ap("%s %.1f C (%.1f F)", lbl, c, cToF(c)); }
static void apPct(const char *lbl, float p) { sep(); ap("%s %.1f %%", lbl, p); }
static void apKpaAbs(const char *lbl, float kpa, uint8_t baro) {
  sep(); ap("%s %.2f kPa abs (%.2f psia", lbl, kpa, kpa * KPA_PSI);
  if (baro) ap(", %.2f psi gauge vs baro", (kpa - baro) * KPA_PSI);
  ap(")");
}
static const char *ctlStatus(uint8_t v) {
  switch (v & 3) { case 1: return "open loop"; case 2: return "closed loop"; case 3: return "FAULT"; default: return "reserved"; }
}
static bool need(int n, int k) {
  if (n >= k) return true;
  sep(); ap("short reply (%d of %d data bytes), raw only", n, k);
  return false;
}

static const char *const OBD_STD[] = {
  "?", "OBD-II (CARB)", "OBD (EPA)", "OBD and OBD-II", "OBD-I", "Not OBD compliant", "EOBD",
  "EOBD and OBD-II", "EOBD and OBD", "EOBD, OBD and OBD-II", "JOBD", "JOBD and OBD-II",
  "JOBD and EOBD", "JOBD, EOBD and OBD-II", "OBD, EOBD and KOBD", "OBD, OBD-II, EOBD and KOBD",
  "Reserved", "EMD", "EMD+", "HD OBD-C", "HD OBD", "WWH OBD", "Reserved", "HD EOBD-I",
  "HD EOBD-I N", "HD EOBD-II", "HD EOBD-II N", "HD ZEV", "OBDBr-1", "OBDBr-2", "KOBD",
  "IOBD I", "IOBD II", "HD EOBD-IV", "OBD, OBD-II and HD OBD", "OBDBr-3"};
static const char *const FUEL[] = {
  "Not available", "Gasoline", "Methanol", "Ethanol", "Diesel", "LPG", "CNG", "Propane",
  "Electric", "Bifuel gasoline", "Bifuel methanol", "Bifuel ethanol", "Bifuel LPG", "Bifuel CNG",
  "Bifuel propane", "Bifuel electricity", "Bifuel electric+combustion", "Hybrid gasoline",
  "Hybrid ethanol", "Hybrid diesel", "Hybrid electric", "Hybrid electric+combustion",
  "Hybrid regenerative", "Bifuel diesel"};

static void monitors(const uint8_t *d, bool withMil) {
  if (withMil) { sep(); ap("MIL %s, %u DTCs", (d[0] & 0x80) ? "ON" : "off", d[0] & 0x7F); }
  bool ci = d[1] & 0x08;
  sep(); ap("%s ignition", ci ? "compression" : "spark");
  static const char *const common[3] = {"misfire", "fuel system", "components"};
  static const char *const diesel[8] = {"NMHC cat", "NOx/SCR", "rsvd2", "boost pressure", "rsvd4",
                                        "exhaust gas sensor", "PM filter", "EGR/VVT"};
  static const char *const spark[8] = {"catalyst", "heated cat", "evap", "secondary air", "GPF",
                                       "O2 sensor", "O2 heater", "EGR/VVT"};
  sep(); ap("incomplete:");
  bool any = false;
  for (int i = 0; i < 3; i++) if ((d[1] & (1 << i)) && (d[1] & (0x10 << i))) { ap(" %s", common[i]); any = true; }
  for (int i = 0; i < 8; i++) if ((d[2] & (1 << i)) && (d[3] & (1 << i))) { ap(" %s", ci ? diesel[i] : spark[i]); any = true; }
  if (!any) ap(" none");
  ap(" (B=%02X C=%02X D=%02X)", d[1], d[2], d[3]);
}

// d = data bytes after "41 pid"; n = count. baro = kPa from PID 33 (0 = unknown).
static void decodePid(uint8_t pid, const uint8_t *d, int n, uint8_t baro, char *o, size_t cap) {
  g_o = o; g_cap = cap; o[0] = 0;
  switch (pid) {
    case 0x01: if (need(n, 4)) monitors(d, true); break;
    case 0x41: if (need(n, 4)) { ap("hex %02X %02X %02X %02X", d[0], d[1], d[2], d[3]); monitors(d, false); } break;
    case 0x04: case 0x2F: case 0x49: case 0x4A: case 0x5A: case 0x8D:
      if (need(n, 1)) ap("%.1f %%", d[0] * 100.0f / 255.0f);
      break;
    case 0x05: case 0x0F: case 0x46: case 0x84:
      if (need(n, 1)) ap("%d C (%.0f F)", d[0] - 40, cToF(d[0] - 40));
      break;
    case 0x0C: if (need(n, 2)) ap("%.1f rpm", u16(d, 0) / 4.0f); break;
    case 0x0D: if (need(n, 1)) ap("%u km/h (%.1f mph)", d[0], d[0] * 0.621371f); break;
    case 0x10: if (need(n, 2)) { float g = u16(d, 0) / 100.0f; ap("%.2f g/s (%.2f lb/min)", g, g * 0.132277f); } break;
    case 0x1C: if (need(n, 1)) ap("%u = %s", d[0], d[0] < sizeof(OBD_STD) / sizeof(OBD_STD[0]) ? OBD_STD[d[0]] : "reserved/other"); break;
    case 0x1F: if (need(n, 2)) ap("%u s (%.1f min)", u16(d, 0), u16(d, 0) / 60.0f); break;
    case 0x21: case 0x31: if (need(n, 2)) ap("%u km (%.0f mi)", u16(d, 0), u16(d, 0) * 0.621371f); break;
    case 0x30: if (need(n, 1)) ap("%u warm-ups", d[0]); break;
    case 0x33: if (need(n, 1)) ap("%u kPa (%.2f psi, %.2f inHg)", d[0], d[0] * KPA_PSI, d[0] * 0.2953f); break;
    case 0x42: if (need(n, 2)) ap("%.3f V", u16(d, 0) / 1000.0f); break;
    case 0x4D: case 0x4E: if (need(n, 2)) ap("%u min (%.1f h)", u16(d, 0), u16(d, 0) / 60.0f); break;
    case 0x51: if (need(n, 1)) ap("%u = %s", d[0], d[0] < sizeof(FUEL) / sizeof(FUEL[0]) ? FUEL[d[0]] : "reserved"); break;
    case 0x5D: if (need(n, 2)) ap("%.2f deg", u16(d, 0) / 128.0f - 210.0f); break;
    case 0x5E: if (need(n, 2)) { float l = u16(d, 0) / 20.0f; ap("%.2f L/h (%.2f gal/h)", l, l * 0.264172f); } break;
    case 0x61: case 0x62: case 0x8E: if (need(n, 1)) ap("%d %%", (int)d[0] - 125); break;
    case 0x63: if (need(n, 2)) ap("%u Nm (%.0f lb-ft)", u16(d, 0), u16(d, 0) * 0.737562f); break;
    case 0x64:
      if (need(n, 5)) ap("idle %d %%; pt1 %d %%; pt2 %d %%; pt3 %d %%; pt4 %d %%",
                         d[0] - 125, d[1] - 125, d[2] - 125, d[3] - 125, d[4] - 125);
      break;
    case 0x65:
      if (need(n, 2)) {
        ap("support %02X status %02X", d[0], d[1]);
        if (d[0] & 1) { sep(); ap("PTO %s", (d[1] & 1) ? "ACTIVE" : "off"); }
        if (d[0] & 2) { sep(); ap("auto trans %s", (d[1] & 2) ? "in gear" : "park/neutral"); }
        if (d[0] & 4) { sep(); ap("manual trans %s", (d[1] & 4) ? "in gear" : "neutral"); }
        if (d[0] & 8) { sep(); ap("glow plug lamp %s", (d[1] & 8) ? "ON" : "off"); }
        ap(" (check)");
      }
      break;
    case 0x66:
      if (need(n, 5)) {
        if (d[0] & 1) { sep(); ap("MAF A %.2f g/s", u16(d, 1) / 32.0f); }
        if (d[0] & 2) { sep(); ap("MAF B %.2f g/s", u16(d, 3) / 32.0f); }
      }
      break;
    case 0x67: case 0x68:
      if (need(n, 3)) { if (d[0] & 1) apTemp("sensor 1", d[1] - 40.0f); if (d[0] & 2) apTemp("sensor 2", d[2] - 40.0f); }
      break;
    case 0x69:
      if (need(n, 7)) {
        if (d[0] & 0x01) apPct("cmd EGR A", d[1] / 2.55f);
        if (d[0] & 0x02) apPct("actual EGR A", d[2] / 2.55f);
        if (d[0] & 0x04) apPct("EGR A error", d[3] / 1.28f - 100.0f);
        if (d[0] & 0x08) apPct("cmd EGR B", d[4] / 2.55f);
        if (d[0] & 0x10) apPct("actual EGR B", d[5] / 2.55f);
        if (d[0] & 0x20) apPct("EGR B error", d[6] / 1.28f - 100.0f);
        ap(" (check)");
      }
      break;
    case 0x6A: case 0x6C: case 0x72:
      if (need(n, 5)) {
        const char *L6A[4] = {"cmd intake air flow A", "rel intake air flow A", "cmd intake air flow B", "rel intake air flow B"};
        const char *L6C[4] = {"cmd throttle A", "rel throttle A pos", "cmd throttle B", "rel throttle B pos"};
        const char *L72[4] = {"cmd wastegate A", "wastegate A", "cmd wastegate B", "wastegate B"};
        const char *const *L = pid == 0x6A ? L6A : (pid == 0x6C ? L6C : L72);
        for (int i = 0; i < 4; i++) if (d[0] & (1 << i)) apPct(L[i], d[1 + i] / 2.55f);
        ap(" (check)");
      }
      break;
    case 0x6B:
      if (need(n, 5)) {
        // byte order B,C,D,E = sensor A(B1S1), C(B1S2), B(B2S1), D(B2S2)
        const char *L[4] = {"EGRT A (B1S1)", "EGRT C (B1S2)", "EGRT B (B2S1)", "EGRT D (B2S2)"};
        for (int i = 0; i < 4; i++) {
          if (d[0] & (0x10 << i)) apTemp(L[i], d[1 + i] * 4.0f - 40.0f);
          else if (d[0] & (1 << i)) apTemp(L[i], d[1 + i] - 40.0f);
        }
        ap(" (check)");
      }
      break;
    case 0x6D:
      if (need(n, 1)) {
        ap("support %02X", d[0]);
        if (n >= 5) {
          if (d[0] & 0x01) { float k = u16(d, 1) * 10.0f; sep(); ap("cmd FRP A %.0f kPa (%.0f psi)", k, k * KPA_PSI); }
          if (d[0] & 0x02) { float k = u16(d, 3) * 10.0f; sep(); ap("actual FRP A %.0f kPa (%.0f psi)", k, k * KPA_PSI); }
        }
        if (n >= 6 && (d[0] & 0x04)) apTemp("rail temp A", d[5] - 40.0f);
        if (n >= 10) {
          if (d[0] & 0x08) { float k = u16(d, 6) * 10.0f; sep(); ap("cmd FRP B %.0f kPa (%.0f psi)", k, k * KPA_PSI); }
          if (d[0] & 0x10) { float k = u16(d, 8) * 10.0f; sep(); ap("actual FRP B %.0f kPa (%.0f psi)", k, k * KPA_PSI); }
        }
        if (n >= 11 && (d[0] & 0x20)) apTemp("rail temp B", d[10] - 40.0f);
      }
      break;
    case 0x6E:
      if (need(n, 9)) {
        const char *L[4] = {"cmd ICP A", "ICP A", "cmd ICP B", "ICP B"};
        for (int i = 0; i < 4; i++) if (d[0] & (1 << i)) { float k = u16(d, 1 + 2 * i) * 10.0f; sep(); ap("%s %.0f kPa (%.0f psi)", L[i], k, k * KPA_PSI); }
        ap(" (check)");
      }
      break;
    case 0x6F:
      if (need(n, 3)) {
        if (d[0] & 4) apKpaAbs("comp inlet A", d[1] * 8.0f, 0); else if (d[0] & 1) apKpaAbs("comp inlet A", d[1], 0);
        if (d[0] & 8) apKpaAbs("comp inlet B", d[2] * 8.0f, 0); else if (d[0] & 2) apKpaAbs("comp inlet B", d[2], 0);
        ap(" (check)");
      }
      break;
    case 0x70:
      if (need(n, 10)) {
        if (d[0] & 0x01) apKpaAbs("cmd boost A", u16(d, 1) / 32.0f, baro);
        if (d[0] & 0x02) apKpaAbs("boost A", u16(d, 3) / 32.0f, baro);
        if (d[0] & 0x04) { sep(); ap("boost A ctl %s", ctlStatus(d[9])); }
        if (d[0] & 0x08) apKpaAbs("cmd boost B", u16(d, 5) / 32.0f, baro);
        if (d[0] & 0x10) apKpaAbs("boost B", u16(d, 7) / 32.0f, baro);
        if (d[0] & 0x20) { sep(); ap("boost B ctl %s", ctlStatus(d[9] >> 2)); }
        ap(" (check)");
      }
      break;
    case 0x71:
      if (need(n, 6)) {
        if (d[0] & 0x01) apPct("cmd VGT A", d[1] / 2.55f);
        if (d[0] & 0x02) apPct("VGT A pos", d[2] / 2.55f);
        if (d[0] & 0x04) { sep(); ap("VGT A ctl %s", ctlStatus(d[5])); }
        if (d[0] & 0x08) apPct("cmd VGT B", d[3] / 2.55f);
        if (d[0] & 0x10) apPct("VGT B pos", d[4] / 2.55f);
        if (d[0] & 0x20) { sep(); ap("VGT B ctl %s", ctlStatus(d[5] >> 2)); }
        ap(" (check)");
      }
      break;
    case 0x73:
      if (need(n, 5)) {
        if (d[0] & 1) apKpaAbs("exh press B1", u16(d, 1) / 100.0f, baro);
        if (d[0] & 2) apKpaAbs("exh press B2", u16(d, 3) / 100.0f, baro);
        ap(" (check)");
      }
      break;
    case 0x74:
      if (need(n, 5)) {
        if (d[0] & 1) { sep(); ap("turbo A %lu rpm", (unsigned long)u16(d, 1) * 10UL); }
        if (d[0] & 2) { sep(); ap("turbo B %lu rpm", (unsigned long)u16(d, 3) * 10UL); }
        ap(" (check)");
      }
      break;
    case 0x75: case 0x76:
      if (need(n, 7)) {
        if (d[0] & 1) apTemp("comp inlet", d[1] - 40.0f);
        if (d[0] & 2) apTemp("comp outlet", d[2] - 40.0f);
        if (d[0] & 4) apTemp("turbine inlet", u16(d, 3) / 10.0f - 40.0f);
        if (d[0] & 8) apTemp("turbine outlet", u16(d, 5) / 10.0f - 40.0f);
        ap(" (check)");
      }
      break;
    case 0x77:
      if (need(n, 5)) {
        const char *L[4] = {"CACT B1S1", "CACT B1S2", "CACT B2S1", "CACT B2S2"};
        for (int i = 0; i < 4; i++) if (d[0] & (1 << i)) apTemp(L[i], d[1 + i] - 40.0f);
        ap(" (check)");
      }
      break;
    case 0x78: case 0x79: case 0x7C:
      if (need(n, 9)) {
        const char *L78[4] = {"EGT1", "EGT2", "EGT3", "EGT4"};
        const char *L7C[4] = {"DPF B1 inlet", "DPF B1 outlet", "DPF B2 inlet", "DPF B2 outlet"};
        const char *const *L = pid == 0x7C ? L7C : L78;
        for (int i = 0; i < 4; i++) if (d[0] & (1 << i)) apTemp(L[i], u16(d, 1 + 2 * i) / 10.0f - 40.0f);
        if (pid == 0x7C) ap(" (check)");
      }
      break;
    case 0x7A: case 0x7B:
      if (need(n, 7)) {
        if (d[0] & 1) { float k = (int16_t)u16(d, 1) / 100.0f; sep(); ap("DPF delta %.2f kPa (%.3f psi, %.1f inH2O)", k, k * KPA_PSI, k * 4.01463f); }
        if (d[0] & 2) apKpaAbs("DPF inlet", u16(d, 3) / 100.0f, baro);
        if (d[0] & 4) apKpaAbs("DPF outlet", u16(d, 5) / 100.0f, baro);
        ap(" (check)");
      }
      break;
    case 0x7D: case 0x7E:
      if (need(n, 1)) {
        ap("hex %02X", d[0]);
        if (d[0] & 1) ap(", inside control area");
        if (d[0] & 2) ap(", outside control area");
        if (d[0] & 4) ap(", in mfr carve-out");
        if (d[0] & 8) ap(", NTE deficiency active");
        ap(" (check)");
      }
      break;
    case 0x7F:
      if (need(n, 13)) {
        const char *L[3] = {"total", "idle", "PTO"};
        for (int i = 0; i < 3; i++) if (d[0] & (1 << i)) {
          uint32_t s = u32(d, 1 + 4 * i);
          sep(); ap("%s %lu s (%.1f h)", L[i], (unsigned long)s, s / 3600.0f);
        }
      }
      break;
    case 0x83:
      if (need(n, 9)) {
        const char *L[4] = {"NOx B1S1", "NOx B1S2", "NOx B2S1", "NOx B2S2"};
        for (int i = 0; i < 4; i++) if (d[0] & (1 << i)) { sep(); ap("%s %u ppm", L[i], u16(d, 1 + 2 * i)); }
        ap(" (check)");
      }
      break;
    case 0x85:
      if (need(n, 10)) {
        if (d[0] & 1) { sep(); ap("avg DEF use %.3f L/h", u16(d, 1) / 200.0f); }
        if (d[0] & 2) { sep(); ap("avg demanded DEF %.3f L/h", u16(d, 3) / 200.0f); }
        if (d[0] & 4) apPct("DEF tank", d[5] / 2.55f);
        if (d[0] & 8) { sep(); ap("NOx warning run time %lu (s? check units)", (unsigned long)u32(d, 6)); }
        ap(" (check)");
      }
      break;
    case 0x86:
      if (need(n, 5)) {
        if (d[0] & 1) { sep(); ap("PM B1S1 %.3f mg/m3", u16(d, 1) / 80.0f); }
        if (d[0] & 2) { sep(); ap("PM B2S1 %.3f mg/m3", u16(d, 3) / 80.0f); }
        ap(" (check)");
      }
      break;
    case 0x87:
      if (need(n, 5)) {
        if (d[0] & 1) apKpaAbs("MAP A", u16(d, 1) / 32.0f, baro);
        if (d[0] & 2) apKpaAbs("MAP B", u16(d, 3) / 32.0f, baro);
        ap(" (check)");
      }
      break;
    case 0x8B:
      if (need(n, 7)) {
        if (d[0] & 0x01) { sep(); ap("DPF regen %s", (d[1] & 1) ? "IN PROGRESS" : "not active"); }
        if (d[0] & 0x02) { sep(); ap("regen type %s", (d[1] & 2) ? "active" : "passive"); }
        if (d[0] & 0x10) apPct("regen trigger", d[2] / 2.55f);
        if (d[0] & 0x20) { sep(); ap("avg time between regens %u min", u16(d, 3)); }
        if (d[0] & 0x40) { sep(); ap("avg dist between regens %u km (%.0f mi)", u16(d, 5), u16(d, 5) * 0.621371f); }
        ap(" (check)");
      }
      break;
    case 0xA2: if (need(n, 2)) ap("%.3f mg/stroke (check)", u16(d, 0) / 32.0f); break;
    case 0xA4:
      if (need(n, 4)) { if (d[0] & 0x02) ap("gear ratio %.3f", u16(d, 2) / 1000.0f); else ap("ratio not supported"); ap(" (gear nibble B=%02X raw) (check)", d[1]); }
      break;
    case 0xA5: if (need(n, 2)) { if (d[0] & 1) ap("%.1f %% (check)", d[1] / 2.0f); else ap("not supported"); } break;
    case 0xA6: if (need(n, 4)) { float km = u32(d, 0) / 10.0f; ap("%.1f km (%.1f mi)", km, km * 0.621371f); } break;
    default:
      if ((pid & 0x1F) == 0 && n >= 4) {
        ap("supports:");
        for (int i = 0; i < 32; i++) if (d[i / 8] & (0x80 >> (i % 8))) ap(" %02X", pid + 1 + i);
      } else {
        ap("raw only");
      }
  }
  if (!o[0]) ap("(no sensors flagged in support byte)");
}

// ---------------------------------------------------------------- records
static Rec recs[MAX_REC];
static int nrec = 0;
static uint8_t g_baro = 0;
static char dec[320];

static void addRec(uint8_t sweep, uint8_t pid, uint8_t ecu, uint8_t kind, const uint8_t *p, uint8_t len) {
  if (nrec >= MAX_REC) return;
  Rec &r = recs[nrec++];
  r.sweep = sweep; r.pid = pid; r.ecu = ecu; r.kind = kind; r.baro = g_baro;
  if (len > sizeof(r.raw)) len = sizeof(r.raw);
  r.len = len;
  if (p && len) memcpy(r.raw, p, len);
}

static void recText(const Rec &r, char *o, size_t cap) {
  if (r.kind == 2) { snprintf(o, cap, "NO RESPONSE"); return; }
  if (r.kind == 1) { snprintf(o, cap, "negative response NRC 0x%02X", r.len >= 3 ? r.raw[2] : 0); return; }
  decodePid(r.pid, r.raw + 2, r.len - 2, r.baro, o, cap);
}

static void printRaw(const uint8_t *p, int n) { for (int i = 0; i < n; i++) { if (i) Serial.print(' '); printHex2(p[i]); } }

// ---------------------------------------------------------------- steps
static Resp rbuf[N_ECU];

static bool wakeUp() {
  Serial.printf("Waiting %d ms after TWAI start...\n", WAKE_DELAY_MS);
  delay(WAKE_DELAY_MS);
  Serial.printf("Wake-up: sending 01 00 to 0x7DF every %d ms (max %d s)\n", WAKE_PERIOD_MS, WAKE_MAX_MS / 1000);
  uint32_t t0 = millis();
  int tries = 0;
  while (millis() - t0 < WAKE_MAX_MS) {
    uint32_t ts = millis();
    tries++;
    int n = transact(CAN_REQ_FUNC, 0x00, 300, rbuf, N_ECU);
    if (n > 0) {
      Serial.printf("  try %d: ANSWERED after %lu ms by", tries, (unsigned long)(millis() - t0));
      for (int i = 0; i < n; i++) Serial.printf(" 0x%03lX", (unsigned long)(CAN_RESP0 + rbuf[i].ecu));
      Serial.println();
      return true;
    }
    Serial.printf("  try %d: no answer yet (%lu ms)%s\n", tries, (unsigned long)(millis() - t0), n == -2 ? " [TX failed]" : "");
    blink(1);
    while (millis() - ts < WAKE_PERIOD_MS) delay(5);
  }
  return false;
}

static void discover() {
  memset(sup, 0, sizeof(sup));
  ecuSeen = 0;
  Serial.println(F("\n-- support discovery (00/20/40/60/80/A0/C0 chain) --"));
  uint8_t base = 0x00;
  while (true) {
    int n = query(base, rbuf, N_ECU, true);
    Serial.printf("PID 0x%02X: ", base);
    if (n <= 0) { Serial.println(F("no response")); break; }
    bool next = false;
    for (int i = 0; i < n; i++) {
      Resp &r = rbuf[i];
      Serial.printf("[0x%03lX] ", (unsigned long)(CAN_RESP0 + r.ecu));
      if (r.len < 6 || r.data[0] != 0x41) { Serial.print(F("raw ")); printRaw(r.data, r.len); Serial.print(' '); continue; }
      ecuSeen |= (1 << r.ecu);
      printRaw(r.data, r.len);
      Serial.print(F(" -> "));
      for (int b = 0; b < 32; b++) if (r.data[2 + b / 8] & (0x80 >> (b % 8))) {
        uint8_t p = base + 1 + b;
        setSup(r.ecu, p);
        printHex2(p); Serial.print(' ');
      }
      if (base < 0xE0 && isSup(r.ecu, base + 0x20)) next = true;
    }
    Serial.println();
    if (!next || base >= 0xC0) break;
    base += 0x20;
    delay(GAP_MS);
  }
  Serial.println(F("\nSupported PIDs by ECU:"));
  for (int e = 0; e < N_ECU; e++) if (ecuSeen & (1 << e)) {
    Serial.printf("  0x%03lX:", (unsigned long)(CAN_RESP0 + e));
    for (int p = 1; p < 256; p++) if (isSup(e, p)) Serial.printf(" %02X", p);
    Serial.println();
  }
}

static void sweep(uint8_t s) {
  Serial.printf("\n==================== SWEEP %u of %u  (t=%lu ms) ====================\n",
                s, SWEEPS, (unsigned long)millis());
  g_baro = 0;
  for (int p = 1; p < 256; p++) {
    if ((p & 0x1F) == 0) continue;               // bitmap PIDs
    if (!anySup(p)) continue;
    int n = query(p, rbuf, N_ECU, false);
    Serial.printf("PID 0x%02X  %s\n", p, pidName(p));
    if (n <= 0) {
      Serial.println(F("  NO RESPONSE (3 tries)"));
      addRec(s, p, 0xFF, 2, nullptr, 0);
    }
    for (int i = 0; i < n; i++) {
      Resp &r = rbuf[i];
      bool neg = (r.data[0] == 0x7F);
      if (!neg && p == 0x33 && r.len >= 3) g_baro = r.data[2];
      addRec(s, p, r.ecu, neg ? 1 : 0, r.data, r.len);
      Serial.printf("  [ECU 0x%03lX] raw: ", (unsigned long)(CAN_RESP0 + r.ecu));
      printRaw(r.data, r.len);
      Serial.println();
      recText(recs[nrec - 1], dec, sizeof(dec));
      Serial.print(F("    -> "));
      Serial.println(dec);
    }
    delay(GAP_MS);
  }
}

static void printSummary() {
  for (uint8_t s = 1; s <= SWEEPS; s++) {
    Serial.printf("\n-- SUMMARY sweep %u --\n", s);
    Serial.println(F("PID  ECU    NAME                             VALUE"));
    for (int i = 0; i < nrec; i++) {
      const Rec &r = recs[i];
      if (r.sweep != s) continue;
      recText(r, dec, sizeof(dec));
      char ecu[8];
      if (r.ecu == 0xFF) snprintf(ecu, sizeof(ecu), "-"); else snprintf(ecu, sizeof(ecu), "0x%03lX", (unsigned long)(CAN_RESP0 + r.ecu));
      Serial.printf("%02X   %-6s %-32s %s\n", r.pid, ecu, pidName(r.pid), dec);
    }
  }
  Serial.println(F("\n--- CSV BEGIN ---"));
  Serial.println(F("pid,ecu,raw_hex,decoded,sweep"));
  for (int i = 0; i < nrec; i++) {
    const Rec &r = recs[i];
    recText(r, dec, sizeof(dec));
    for (char *c = dec; *c; c++) if (*c == '"') *c = '\'';
    Serial.printf("%02X,", r.pid);
    if (r.ecu == 0xFF) Serial.print('-'); else Serial.printf("%03lX", (unsigned long)(CAN_RESP0 + r.ecu));
    Serial.print(',');
    printRaw(r.raw, r.len);
    Serial.printf(",\"%s\",%u\n", dec, r.sweep);
  }
  Serial.println(F("--- CSV END ---"));
}

// ---------------------------------------------------------------- main
void setup() {
  pinMode(PIN_LED, OUTPUT);
  led(true);
  Serial.begin(115200);
  uint32_t tWait = millis();
  while (!Serial && millis() - tWait < 4000) { delay(10); }
  delay(200);

  Serial.println();
  Serial.println(F("=== pid_probe_C6 v2  (read-only Mode 01 full sweep x3, then stop) ==="));
  Serial.println(F("*** Best data with the ENGINE RUNNING (idle is fine; rev it during sweeps 2-3). ***"));
  Serial.println(F("XIAO ESP32-C6  D3/GPIO21 = CTX  D4/GPIO22 = CRX  500 kbit/s NORMAL (not listen-only)"));
  Serial.println(F("Requests: 0x7DF functional, fallback 0x7E0+n physical. Replies: 0x7E8..0x7EF"));
  Serial.println(F("'(check)' = formula from one secondary source; 'raw only' = no confident decode."));
  Serial.println();

  if (!startCan()) {
    Serial.println(F("TWAI init FAILED"));
    while (true) { blink(5); delay(1000); }
  }

  if (!wakeUp()) {
    Serial.println(F("No ECU answered in ~15 s. Check key ON, CTX on D3, CAN-C pins 6/14, GND pin 5."));
    Serial.println(F("Trying discovery once anyway..."));
  }
  discover();
  if (!ecuSeen) {
    Serial.println(F("No support bitmaps received; nothing to sweep."));
  } else {
    for (uint8_t s = 1; s <= SWEEPS; s++) {
      sweep(s);
      if (s < SWEEPS) delay(SWEEP_GAP_MS);
    }
    printSummary();
  }
  Serial.println();
  Serial.println(F("PROBE COMPLETE. Sketch is stopped (not polling). You may unplug or reflash the sniffer."));
  Serial.flush();
  led(false);
}

void loop() {
  delay(1000);
  blink(1);
}
