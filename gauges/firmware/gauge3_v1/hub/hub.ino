// =============================================================================
// gauge3_v1 / hub.ino  -  under-dash hub for the 3-gauge A-pillar pod
// Truck : 2017 Ram 3500 6.7 Cummins (pre-SGW), OBD-II CAN 500 kbps, 11-bit
// Board : ESP32-S3-DevKitC style, ESP32-S3-WROOM-1 N16R8 (16 MB flash, 8 MB OPI PSRAM)
// FQBN  : esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc,USBMode=hwcdc
//
// Parts : SN65HVD230 CAN transceiver (TWAI), MAX31855 K-type amp (EGT, cyl-6 pre-turbo),
//         EC11 rotary encoder with push switch.
// Link  : ESP-NOW broadcast (g3_packet.h) to 3 Waveshare ESP32-S3-Touch-LCD-2.1 gauges, 20 Hz.
// USB   : serial 115200 on BOTH the native "USB" port and the "UART/COM" port. Type "help".
// =============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <Adafruit_MAX31855.h>
#include "g3_packet.h"
#include "obd.h"
#include "obd_pids.h"
#include "knob.h"

#define FW_VERSION "gauge3_v1 hub 1.1"

// ----------------------------------------------------------------- PIN TABLE
// (ESP32-S3-WROOM-1 N16R8: GPIO26-32 = flash, GPIO33-37 = OPI PSRAM, GPIO19/20 = USB,
//  GPIO0/3/45/46 = strapping, GPIO43/44 = UART0 -> none of those are used)
#define PIN_CAN_TX    4    // -> SN65HVD230 CTX / D
#define PIN_CAN_RX    5    // <- SN65HVD230 CRX / R
#define PIN_EGT_SCK   12   // -> MAX31855 SCK / CLK
#define PIN_EGT_SO    13   // <- MAX31855 SO / DO
#define PIN_EGT_CS    10   // -> MAX31855 CS
#define PIN_ENC_A     16   // <- EC11 A  (CLK)
#define PIN_ENC_B     17   // <- EC11 B  (DT)
#define PIN_ENC_SW    18   // <- EC11 SW (push, to GND)

// ----------------------------------------------------------------- tuning
#define FOCUS_IDLE_MS      8000   // focus auto-clears after this idle time
#define G2_BACK_TO_BOOST_MS 300   // EXH BRAKE -> BOOST delay (BOOST -> EXH BRAKE delay is a knob setting)
#define RAIL_RUNNING_RPM   400    // engine counts as running above this (RAIL LOW check)
#define G2_PEDAL_LOW_PCT   5.0f   // pedal below this = "off throttle"
#define G2_PEDAL_ON_PCT    10.0f  // pedal above this = back to BOOST
#define G2_EBP_MIN_PSI     8.0f   // EBP must be at least this ...
#define G2_EBP_OVER_BOOST  5.0f   // ... or this much above boost ("EBP rising")
#define BATT_LOW_MS        3000   // battery must be low this long (cranking dips)
#define KPA_TO_PSI         0.1450377f

// ----------------------------------------------------------------- console on both USB ports
class DualPrint : public Print {
 public:
  size_t write(uint8_t c) override { Serial.write(c); Serial0.write(c); return 1; }
  size_t write(const uint8_t* b, size_t n) override { Serial.write(b, n); Serial0.write(b, n); return n; }
};
static DualPrint Con;

static Preferences prefs;
static Adafruit_MAX31855 tc(PIN_EGT_SCK, PIN_EGT_CS, PIN_EGT_SO);   // software SPI (read-only chip)

// ----------------------------------------------------------------- state
static G3Packet pk;                // the packet being built (also the hub's UI state)
static float val[V_COUNT];
static uint16_t valid = 0;
static bool ov_on[V_COUNT];        // "set" overrides
static float ov_val[V_COUNT];
static bool sweep = false;
static uint32_t sweep_t0 = 0;
static bool watch = false;
static float egt_f = NAN; static uint32_t egt_ms = 0; static bool probe_ok = false; static uint8_t tc_fault = 0;
static bool editing = false; static float edit_val = 0;
static uint32_t last_input = 0, bright_save_at = 0;
static bool g2_ebp = false; static uint32_t g2_t = 0;
static uint32_t mm_frp_t = 0, mm_frp_clr = 0, low_frp_t = 0, low_frp_clr = 0, batt_t = 0;
static uint32_t muted_bits = 0;
static uint32_t seq = 0;
static const char* VNAME[V_COUNT] = {"egt", "boost", "ebp", "frp_act", "frp_cmd", "load", "pedal", "iat", "batt", "rpm", "brake_hp"};
static const char* VUNIT[V_COUNT] = {"F", "psi", "psi", "psi", "psi", "%", "%", "F", "V", "rpm", "hp"};
static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static inline bool V(int i) { return (valid >> i) & 1; }

// ----------------------------------------------------------------- settings in NVS
static void settings_load() {
  for (int i = 0; i < S_COUNT; i++) {
    char k[8]; snprintf(k, sizeof k, "s%d", i);
    float v = prefs.getFloat(k, G3_SETTINGS[i].def);
    if (v < G3_SETTINGS[i].mn || v > G3_SETTINGS[i].mx) v = G3_SETTINGS[i].def;
    pk.set_val[i] = v;
  }
  pk.brightness = prefs.getUChar("bright", 70);
}
static void setting_save(int i) { char k[8]; snprintf(k, sizeof k, "s%d", i); prefs.putFloat(k, pk.set_val[i]); }
static float S(int i) { return pk.set_val[i]; }

// ----------------------------------------------------------------- bench sweep (30 s cycle)
// 0-4 s idle, 4-16 s towing up a grade (EGT spike past ALARM at ~11 s, rail lag at 8-11 s, boost spike 13-14.5 s),
// 16-26 s exhaust braking (EBP ~58 psi, brake ~80 HP), 26-30 s idle, battery dip 25-30 s.
static void sweep_values(float* t_out, uint16_t* vmask, bool egt_from_probe) {
  static float st[V_COUNT]; static bool init = false; static uint32_t last = 0;
  float tgt[V_COUNT];
  float t = fmodf((millis() - sweep_t0) / 1000.f, 30.f);
  bool tow = t >= 4 && t < 16, brake = t >= 16 && t < 26;
  tgt[V_PEDAL] = tow ? 85 : 0;
  tgt[V_RPM]   = tow ? 2300 : brake ? 1900 : 750;
  tgt[V_BOOST] = tow ? (t > 13 && t < 14.5f ? 39 : 30) : brake ? 2.5f : 0.5f;
  tgt[V_EBP]   = tow ? 45 : brake ? 58 : 1;
  tgt[V_BRAKE_HP] = brake ? 80 : 0;
  tgt[V_EGT]   = tow ? (t > 10.5f && t < 13 ? 1440 : 700 + (t - 4) * 50) : brake ? 650 : 380;
  tgt[V_FRP_CMD] = tow ? 26000 : brake ? 6000 : 5000;
  tgt[V_FRP_ACT] = tgt[V_FRP_CMD] - (t > 8 && t < 11.5f ? 4200 : 150);
  tgt[V_LOAD]  = tow ? 90 : brake ? 4 : 16;
  tgt[V_IAT]   = tow ? 95 + (t - 4) * 2.5f : 100;
  tgt[V_BATT]  = t > 25 ? 11.6f : 14.1f;
  uint32_t now = millis();
  float dt = init ? (now - last) / 1000.f : 10.f; last = now;
  float k = dt / (0.6f + dt);
  for (int i = 0; i < V_COUNT; i++) { st[i] = init ? st[i] + k * (tgt[i] - st[i]) : tgt[i]; }
  init = true;
  for (int i = 0; i < V_COUNT; i++) {
    if (i == V_EGT && egt_from_probe) continue;
    t_out[i] = st[i]; *vmask |= 1 << i;
  }
}

// ----------------------------------------------------------------- inputs -> values
static void read_egt() {
  static uint32_t next = 0;
  if (millis() < next) return;
  next = millis() + 200;                          // MAX31855 converts every ~100 ms
  double c = tc.readCelsius();
  tc_fault = tc.readError();
  if (isnan(c) || tc_fault) { probe_ok = false; return; }
  float f = c * 9.0 / 5.0 + 32.0;
  egt_f = isnan(egt_f) ? f : egt_f + 0.5f * (f - egt_f);
  egt_ms = millis(); probe_ok = true;
}

static void compute_values() {
  valid = 0;
  float x;
  float baro = obd_value(R_BARO_KPA, &x, 10000) ? x : 101.3f;
  if (probe_ok && millis() - egt_ms < 1000) { val[V_EGT] = egt_f; valid |= 1 << V_EGT; }
  if (obd_value(R_MAP_KPA, &x))     { val[V_BOOST] = fmaxf(0, (x - baro) * KPA_TO_PSI); valid |= 1 << V_BOOST; }
  if (obd_value(R_EXH_KPA, &x))     { val[V_EBP] = fmaxf(0, (x - baro) * KPA_TO_PSI); valid |= 1 << V_EBP; }
  if (obd_value(R_FRP_ACT_KPA, &x)) { val[V_FRP_ACT] = x * KPA_TO_PSI; valid |= 1 << V_FRP_ACT; }
  if (obd_value(R_FRP_CMD_KPA, &x)) { val[V_FRP_CMD] = x * KPA_TO_PSI; valid |= 1 << V_FRP_CMD; }
  if (obd_value(R_LOAD, &x))        { val[V_LOAD] = x; valid |= 1 << V_LOAD; }
  if (obd_value(R_PEDAL, &x))       { val[V_PEDAL] = x; valid |= 1 << V_PEDAL; }
  if (obd_value(R_IAT_C, &x, 5000)) { val[V_IAT] = x * 9.f / 5.f + 32.f; valid |= 1 << V_IAT; }
  if (obd_value(R_VOLT, &x, 5000))  { val[V_BATT] = x; valid |= 1 << V_BATT; }
  if (obd_value(R_RPM, &x, 5000))   { val[V_RPM] = x; valid |= 1 << V_RPM; }
  if (obd_value(R_EB_HP, &x, 2000)) { val[V_BRAKE_HP] = x; valid |= 1 << V_BRAKE_HP; }   // broadcast 0x30A, 100 ms
  if (sweep) sweep_values(val, &valid, (valid >> V_EGT) & 1);
  for (int i = 0; i < V_COUNT; i++) if (ov_on[i]) { val[i] = ov_val[i]; valid |= 1 << i; }
}

static void peaks_update() {
  for (int i = 0; i < V_COUNT; i++) {
    if (!V(i)) continue;
    if (i == V_BATT) { if (val[i] > 5 && (pk.peak[i] <= 0 || val[i] < pk.peak[i])) pk.peak[i] = val[i]; }
    else if (val[i] > pk.peak[i]) pk.peak[i] = val[i];
  }
}
static void peaks_reset(int g) {   // g = 1..3, 0 = all
  static const int G1[] = {V_EGT}, G2[] = {V_BOOST, V_EBP, V_BRAKE_HP}, G3[] = {V_FRP_ACT, V_FRP_CMD, V_LOAD, V_PEDAL, V_IAT, V_BATT, V_RPM};
  auto clr = [&](const int* l, int n) { for (int i = 0; i < n; i++) pk.peak[l[i]] = (l[i] == V_BATT) ? 0 : (V(l[i]) ? val[l[i]] : 0); };
  if (g == 0 || g == 1) { clr(G1, 1); pk.reset_count[0]++; }
  if (g == 0 || g == 2) { clr(G2, 3); pk.reset_count[1]++; }
  if (g == 0 || g == 3) { clr(G3, 7); pk.reset_count[2]++; }
}

// ----------------------------------------------------------------- alarms
static void hyst(uint32_t bit, bool ok, float v, float on, float off_below) {
  if (!ok) { pk.alarms &= ~bit; return; }
  if (v >= on) pk.alarms |= bit; else if (v < off_below) pk.alarms &= ~bit;
}
static void alarms_update() {
  uint32_t now = millis();
  hyst(A_EGT_WARN,   V(V_EGT),   val[V_EGT],   S(S_EGT_WARN),   S(S_EGT_WARN) - 25);
  hyst(A_EGT_ALARM,  V(V_EGT),   val[V_EGT],   S(S_EGT_ALARM),  S(S_EGT_ALARM) - 50);
  hyst(A_BOOST_WARN, V(V_BOOST), val[V_BOOST], S(S_BOOST_WARN), S(S_BOOST_WARN) - 2);
  hyst(A_BOOST_ALARM,V(V_BOOST), val[V_BOOST], S(S_BOOST_ALARM),S(S_BOOST_ALARM) - 2);
  hyst(A_EBP_WARN,   V(V_EBP),   val[V_EBP],   S(S_EBP_WARN),   S(S_EBP_WARN) - 2);
  hyst(A_EBP_ALARM,  V(V_EBP),   val[V_EBP],   S(S_EBP_ALARM),  S(S_EBP_ALARM) - 2);
  hyst(A_IAT_WARN,   V(V_IAT),   val[V_IAT],   S(S_IAT_WARN),   S(S_IAT_WARN) - 5);
  // battery low for BATT_LOW_MS
  if (V(V_BATT) && val[V_BATT] > 5 && val[V_BATT] < S(S_BATT_LOW)) { if (!batt_t) batt_t = now; if (now - batt_t >= BATT_LOW_MS) pk.alarms |= A_BATT_LOW; }
  else { batt_t = 0; if (!V(V_BATT) || val[V_BATT] > S(S_BATT_LOW) + 0.3f) pk.alarms &= ~A_BATT_LOW; }
  // rail pressure: actual LAGS commanded by more than RAIL LAG for > G3_MISMATCH_MS (1 s)
  bool fok = V(V_FRP_ACT) && V(V_FRP_CMD) && val[V_FRP_CMD] > 2000;
  float flag = fok ? val[V_FRP_CMD] - val[V_FRP_ACT] : 0;
  if (fok && flag > S(S_FRP_MM)) { mm_frp_clr = 0; if (!mm_frp_t) mm_frp_t = now; if (now - mm_frp_t >= G3_MISMATCH_MS) pk.alarms |= A_FRP_MM; }
  else { mm_frp_t = 0; if (!fok || flag < 0.7f * S(S_FRP_MM)) { if (!mm_frp_clr) mm_frp_clr = now; if (now - mm_frp_clr >= 1000) pk.alarms &= ~A_FRP_MM; } }
  // rail pressure: actual below RAIL LOW with the engine running, for > G3_MISMATCH_MS
  bool lok = V(V_FRP_ACT) && V(V_RPM) && val[V_RPM] > RAIL_RUNNING_RPM;
  if (lok && val[V_FRP_ACT] < S(S_FRP_LOW)) { low_frp_clr = 0; if (!low_frp_t) low_frp_t = now; if (now - low_frp_t >= G3_MISMATCH_MS) pk.alarms |= A_FRP_LOW; }
  else { low_frp_t = 0; if (!lok || val[V_FRP_ACT] > S(S_FRP_LOW) + 300) { if (!low_frp_clr) low_frp_clr = now; if (now - low_frp_clr >= 1000) pk.alarms &= ~A_FRP_LOW; } }
  // mute: stays until all buzzing alarms clear; a NEW alarm un-mutes
  uint32_t buzz = pk.alarms & A_BUZZ_ALL;
  if (!buzz) { pk.flags &= ~F_MUTED; muted_bits = 0; }
  else if ((pk.flags & F_MUTED) && (buzz & ~muted_bits)) pk.flags &= ~F_MUTED;
}

// ----------------------------------------------------------------- gauge 2 BOOST / EBP auto switch
// v2: when the 0x30A broadcast is present, EXH BRAKE mode = brake working (0x30A b7 > 0) for SWITCH DELAY;
// back to BOOST when b7 = 0 or the pedal is pressed for 0.3 s. Without 0x30A: the older pedal + EBP rule.
static void g2_mode_update() {
  uint32_t now = millis();
  if (!V(V_EBP)) { g2_ebp = false; g2_t = 0; return; }
  if (V(V_BRAKE_HP)) {
    bool braking = val[V_BRAKE_HP] > 0.5f && !(V(V_PEDAL) && val[V_PEDAL] > G2_PEDAL_ON_PCT);
    if (!g2_ebp) {
      if (braking) { if (!g2_t) g2_t = now; if (now - g2_t >= (uint32_t)(S(S_G2_DELAY) * 1000)) { g2_ebp = true; g2_t = 0; } }
      else g2_t = 0;
    } else {
      if (!braking) { if (!g2_t) g2_t = now; if (now - g2_t >= G2_BACK_TO_BOOST_MS) { g2_ebp = false; g2_t = 0; } }
      else g2_t = 0;
    }
    return;
  }
  bool pedal_ok = V(V_PEDAL);
  bool off_throttle = pedal_ok ? val[V_PEDAL] < G2_PEDAL_LOW_PCT : val[V_BOOST] < 4;
  bool ebp_rising = val[V_EBP] >= G2_EBP_MIN_PSI || val[V_EBP] - val[V_BOOST] >= G2_EBP_OVER_BOOST;
  bool want_boost = pedal_ok ? val[V_PEDAL] > G2_PEDAL_ON_PCT : val[V_BOOST] > 6;
  if (!g2_ebp) {
    if (off_throttle && ebp_rising) { if (!g2_t) g2_t = now; if (now - g2_t >= (uint32_t)(S(S_G2_DELAY) * 1000)) { g2_ebp = true; g2_t = 0; } }
    else g2_t = 0;
  } else {
    if (want_boost || val[V_EBP] < 5) { if (!g2_t) g2_t = now; if (now - g2_t >= G2_BACK_TO_BOOST_MS) { g2_ebp = false; g2_t = 0; } }
    else g2_t = 0;
  }
}

// ----------------------------------------------------------------- knob UI
static bool alarm_sounding() { return (pk.alarms & A_BUZZ_ALL) && !(pk.flags & F_MUTED); }
static void leave_gauge(int g) { if (g >= 0 && g < 3) pk.page[g] = PAGE_MAIN; editing = false; }

static void on_turn(int d) {
  last_input = millis();
  if (pk.focus == FOCUS_NONE) {                    // dim / brighten all 3
    int b = pk.brightness + d * 5; if (b < 5) b = 5; if (b > 100) b = 100;
    if (b != pk.brightness) { pk.brightness = b; pk.bright_count++; bright_save_at = millis() + 3000; }
    return;
  }
  if (pk.focus == FOCUS_ALL) {                     // all 3: MAIN <-> PEAK
    uint8_t np = pk.page[0] == PAGE_PEAK ? PAGE_MAIN : PAGE_PEAK;
    for (int g = 0; g < 3; g++) pk.page[g] = np;
    return;
  }
  int g = pk.focus - 1, gn = g + 1;
  if (pk.page[g] == PAGE_SETTINGS) {
    int n = g3_settings_count(gn);
    if (editing) {
      const SettingDef& sd = G3_SETTINGS[g3_settings_id(gn, pk.set_idx)];
      edit_val += d * sd.step; if (edit_val < sd.mn) edit_val = sd.mn; if (edit_val > sd.mx) edit_val = sd.mx;
      edit_val = roundf(edit_val / sd.step) * sd.step;
      return;
    }
    int i = (int)pk.set_idx + d;
    if (i < 0) { pk.page[g] = PAGE_PEAK; return; }                         // scroll above first item -> PEAK
    if (i >= n) { pk.page[g] = PAGE_MAIN; if (g == 2) pk.util_page = 0; return; }   // past last -> MAIN
    pk.set_idx = i; return;
  }
  if (g == 2) {   // gauge 3: utility pages, then PEAK, then SETTINGS (wraps)
    int pos = pk.page[2] == PAGE_MAIN ? pk.util_page : (pk.page[2] == PAGE_PEAK ? UP_COUNT : UP_COUNT + 1);
    int total = UP_COUNT + 2;
    pos = ((pos + d) % total + total) % total;
    if (pos < UP_COUNT) { pk.page[2] = PAGE_MAIN; pk.util_page = pos; }
    else if (pos == UP_COUNT) pk.page[2] = PAGE_PEAK;
    else { pk.page[2] = PAGE_SETTINGS; pk.set_idx = d > 0 ? 0 : g3_settings_count(3) - 1; }
    return;
  }
  int p = ((int)pk.page[g] + d + PAGE_COUNT * 4) % PAGE_COUNT;              // MAIN -> PEAK -> SETTINGS
  pk.page[g] = p;
  if (p == PAGE_SETTINGS) pk.set_idx = d > 0 ? 0 : g3_settings_count(gn) - 1;
}

static void on_click() {
  if (alarm_sounding()) {                          // ANY click mutes, nothing else changes
    pk.flags |= F_MUTED; muted_bits = pk.alarms & A_BUZZ_ALL; return;
  }
  last_input = millis();
  if (pk.focus >= FOCUS_G1 && pk.focus <= FOCUS_G3 && pk.page[pk.focus - 1] == PAGE_SETTINGS) {
    int id = g3_settings_id(pk.focus, pk.set_idx);
    if (!editing) { editing = true; edit_val = pk.set_val[id]; }
    else { pk.set_val[id] = edit_val; setting_save(id); editing = false; }
    return;
  }
  if (pk.focus >= FOCUS_G1 && pk.focus <= FOCUS_G3) leave_gauge(pk.focus - 1);
  if (pk.focus == FOCUS_ALL) for (int g = 0; g < 3; g++) leave_gauge(g);
  pk.focus = (pk.focus + 1) % FOCUS_COUNT;          // EGT -> G2 -> G3 -> ALL -> none
}

static void on_hold() {
  last_input = millis();
  if (editing) { editing = false; return; }        // cancel edit
  if (pk.focus == FOCUS_G3 && pk.page[2] == PAGE_MAIN && pk.util_page == UP_TILT) { pk.imu_zero_count++; return; }
  if (pk.focus >= FOCUS_G1 && pk.focus <= FOCUS_G3) peaks_reset(pk.focus);
  else peaks_reset(0);
}

static void focus_timeout() {
  if (pk.focus == FOCUS_NONE || millis() - last_input < FOCUS_IDLE_MS) return;
  for (int g = 0; g < 3; g++) leave_gauge(g);
  pk.focus = FOCUS_NONE;
}

// ----------------------------------------------------------------- serial console
static void print_help() {
  Con.println(F("\n" FW_VERSION " - USB commands (115200 baud, end with Enter):"));
  Con.println(F("  help                 this list"));
  Con.println(F("  status               all values, source, alarms, link"));
  Con.println(F("  watch [on|off]       print values 2x per second"));
  Con.println(F("  sweep [on|off]       automatic 30 s bench sweep of all values"));
  Con.println(F("  set <name> <value>   force a value. names:"));
  Con.println(F("                       egt boost ebp frp_act frp_cmd load pedal iat batt rpm brake_hp"));
  Con.println(F("  clear <name|all>     remove a forced value (back to sensor / CAN / sweep)"));
  Con.println(F("  probe                query ECM supported PIDs and print the report (truck, key ON)"));
  Con.println(F("  settings             list alarm settings (index, value, default)"));
  Con.println(F("  setting <i> <value>  change setting i (saved)"));
  Con.println(F("  defaults             restore all settings to starting values"));
  Con.println(F("  click | hold | left | right    simulate the knob"));
  Con.println(F("  Real MAX31855 EGT is used whenever a probe is attached, unless 'set egt' is active."));
}
static const char* src_name(int i) {
  if (ov_on[i]) return "SET";
  if (i == V_EGT && probe_ok) return "MAX31855";
  if (sweep) return "SWEEP";
  return V(i) ? "CAN" : "--";
}
static void print_status() {
  Con.printf("\n%s  seq=%lu  CAN=%s  ECM PIDs=%s  rx=%lu txerr=%lu  EGT probe=%s (fault 0x%02X)\n", FW_VERSION,
             (unsigned long)seq, obd_can_ok() ? "OK" : "no answer", obd_discovered() ? "read" : "not yet",
             (unsigned long)obd_rx_frames(), (unsigned long)obd_tx_errors(), probe_ok ? "OK" : "NOT OK", tc_fault);
  for (int i = 0; i < V_COUNT; i++)
    Con.printf("  %-8s %10.1f %-4s  peak %10.1f  [%s]\n", VNAME[i], V(i) ? val[i] : NAN, VUNIT[i], pk.peak[i], src_name(i));
  Con.printf("  gauge2=%s  focus=%d  pages=%d/%d/%d util=%d  bright=%d%%  alarms=0x%03lX%s\n", g2_ebp ? "EXH BRAKE" : "BOOST",
             pk.focus, pk.page[0], pk.page[1], pk.page[2], pk.util_page, pk.brightness, (unsigned long)pk.alarms,
             (pk.flags & F_MUTED) ? " MUTED" : "");
}
static void print_settings() {
  Con.println(F("  i  NAME          VALUE     DEFAULT (editable starting values, not truck specs)"));
  for (int i = 0; i < S_COUNT; i++)
    Con.printf("  %-2d %-13s %8.1f  %8.1f %s\n", i, G3_SETTINGS[i].name, pk.set_val[i], G3_SETTINGS[i].def,
               G3_SETTINGS[i].unit[0] == 0x7f ? "F" : G3_SETTINGS[i].unit);
}
static int vindex(const char* n) { for (int i = 0; i < V_COUNT; i++) if (!strcasecmp(n, VNAME[i])) return i; return -1; }

static void handle_line(char* line) {
  char* cmd = strtok(line, " \t"); if (!cmd) return;
  char* a1 = strtok(nullptr, " \t"); char* a2 = strtok(nullptr, " \t");
  if (!strcasecmp(cmd, "help") || !strcmp(cmd, "?")) print_help();
  else if (!strcasecmp(cmd, "status")) print_status();
  else if (!strcasecmp(cmd, "watch")) { watch = !(a1 && !strcasecmp(a1, "off")); Con.printf("watch %s\n", watch ? "on" : "off"); }
  else if (!strcasecmp(cmd, "sweep")) { sweep = !(a1 && !strcasecmp(a1, "off")); sweep_t0 = millis(); Con.printf("sweep %s\n", sweep ? "ON (30 s cycle)" : "off"); }
  else if (!strcasecmp(cmd, "set")) {
    int i = a1 ? vindex(a1) : -1;
    if (i < 0 || !a2) { Con.println(F("usage: set <name> <value>   (type help for names)")); return; }
    ov_on[i] = true; ov_val[i] = atof(a2); Con.printf("%s = %.1f %s (forced)\n", VNAME[i], ov_val[i], VUNIT[i]);
  } else if (!strcasecmp(cmd, "clear")) {
    if (a1 && !strcasecmp(a1, "all")) { for (int i = 0; i < V_COUNT; i++) ov_on[i] = false; Con.println(F("all forced values cleared")); return; }
    int i = a1 ? vindex(a1) : -1;
    if (i < 0) { Con.println(F("usage: clear <name|all>")); return; }
    ov_on[i] = false; Con.printf("%s cleared\n", VNAME[i]);
  } else if (!strcasecmp(cmd, "probe")) { Con.println(F("probe started (takes a few seconds)...")); obd_request_probe(); }
  else if (!strcasecmp(cmd, "settings")) print_settings();
  else if (!strcasecmp(cmd, "setting")) {
    int i = a1 ? atoi(a1) : -1;
    if (i < 0 || i >= S_COUNT || !a2) { Con.println(F("usage: setting <i> <value>")); return; }
    float v = atof(a2); const SettingDef& d = G3_SETTINGS[i];
    if (v < d.mn || v > d.mx) { Con.printf("out of range %.1f..%.1f\n", d.mn, d.mx); return; }
    pk.set_val[i] = v; setting_save(i); Con.printf("%s = %.1f (saved)\n", d.name, v);
  } else if (!strcasecmp(cmd, "defaults")) { for (int i = 0; i < S_COUNT; i++) { pk.set_val[i] = G3_SETTINGS[i].def; setting_save(i); } Con.println(F("settings = starting values")); }
  else if (!strcasecmp(cmd, "click")) on_click();
  else if (!strcasecmp(cmd, "hold")) on_hold();
  else if (!strcasecmp(cmd, "left")) on_turn(-1);
  else if (!strcasecmp(cmd, "right")) on_turn(+1);
  else Con.println(F("unknown command - type help"));
}
static void serial_poll(Stream& s, char* buf, int& n) {
  while (s.available()) {
    char c = s.read();
    if (c == '\r') continue;
    if (c == '\n') { buf[n] = 0; if (n) handle_line(buf); n = 0; continue; }
    if (n < 79) buf[n++] = c;
  }
}

// ----------------------------------------------------------------- packet
static void send_packet() {
  pk.seq = ++seq; pk.hub_ms = millis();
  memcpy(pk.val, val, sizeof val);
  pk.valid = valid;
  uint16_t f = pk.flags & F_MUTED;
  bool test = sweep; for (int i = 0; i < V_COUNT; i++) test |= ov_on[i];
  if (test) f |= F_TEST_MODE;
  if (sweep) f |= F_SWEEP;
  if (obd_can_ok()) f |= F_CAN_OK;
  if (probe_ok) f |= F_EGT_PROBE;
  if (g2_ebp) f |= F_G2_EBP;
  if (editing) f |= F_SET_EDIT;
  if (obd_ebp_supported() || V(V_EBP)) f |= F_EBP_SUPPORTED;
  if (V(V_BRAKE_HP)) f |= F_EB_BCAST;
  pk.flags = f;
  pk.edit_val = edit_val;
  g3_seal(pk);
  esp_now_send(BCAST, (const uint8_t*)&pk, sizeof pk);
}

// ----------------------------------------------------------------- setup / loop
void setup() {
  Serial.begin(115200);      // native USB (CDC)
  Serial0.begin(115200);     // UART0 (USB-UART bridge port)
  delay(300);
  memset(&pk, 0, sizeof pk);
  prefs.begin("g3hub", false);
  settings_load();
  tc.begin();
  knob_begin(PIN_ENC_A, PIN_ENC_B, PIN_ENC_SW);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(G3_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) Con.println(F("ESP-NOW init FAILED"));
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BCAST, 6);
  peer.channel = G3_WIFI_CHANNEL; peer.ifidx = WIFI_IF_STA; peer.encrypt = false;
  esp_now_add_peer(&peer);

  obd_begin(PIN_CAN_TX, PIN_CAN_RX, &Con);
  Con.printf("%s ready. MAC %s. Type 'help'.\n", FW_VERSION, WiFi.macAddress().c_str());
}

void loop() {
  static char b1[80], b2[80]; static int n1 = 0, n2 = 0;
  serial_poll(Serial, b1, n1);
  serial_poll(Serial0, b2, n2);

  KnobEvents e = knob_poll();
  if (e.click) on_click();
  if (e.hold) on_hold();
  if (e.turn) on_turn(e.turn);
  focus_timeout();
  if (bright_save_at && millis() > bright_save_at) { prefs.putUChar("bright", pk.brightness); bright_save_at = 0; }

  read_egt();

  static uint32_t next_tx = 0, next_watch = 0;
  if (millis() >= next_tx) {
    next_tx = millis() + G3_TX_PERIOD_MS;
    compute_values();
    peaks_update();
    alarms_update();
    g2_mode_update();
    send_packet();
  }
  if (watch && millis() >= next_watch) {
    next_watch = millis() + 500;
    Con.printf("EGT %5.0f%s BOOST %4.1f EBP %4.1f BRK %3.0fHP [%s] RAIL %5.0f/%5.0f LOAD %3.0f PEDAL %3.0f IAT %3.0f BATT %4.1f ALM 0x%03lX%s\n",
               V(V_EGT) ? val[V_EGT] : NAN, probe_ok ? "*" : " ", val[V_BOOST], val[V_EBP], V(V_BRAKE_HP) ? val[V_BRAKE_HP] : NAN, g2_ebp ? "EXH" : "BST",
               val[V_FRP_ACT], val[V_FRP_CMD], val[V_LOAD], val[V_PEDAL], val[V_IAT], val[V_BATT],
               (unsigned long)pk.alarms, (pk.flags & F_MUTED) ? " MUTED" : "");
  }
}
