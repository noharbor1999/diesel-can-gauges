// obd.cpp - see obd.h. Runs in its own FreeRTOS task.
#include "obd.h"
#include "obd_pids.h"
#include <Arduino.h>
#include "driver/twai.h"

#define OBD_REQ_ID   0x7DF
#define OBD_ECM_RESP 0x7E8
#define OBD_ECM_PHYS 0x7E0     // flow-control target for ECM multi-frame answers
#define OBD_TIMEOUT_MS 60      // wait for first answer frame
#define OBD_MF_TIMEOUT_MS 150  // whole multi-frame answer
#define BC_EB_ID     0x30A     // CAN-C broadcast, 100 ms: byte 2 = exhaust-brake switch, byte 7 = brake effort (dash HP / 1.32)
#define BC_EB_HP_K   1.32f     // b7 -> HP, from the brake-HP voice calibration (obd2-sniff decode README)

static Print* LOG = nullptr;
static volatile float raw_v[R_COUNT];
static volatile uint32_t raw_ms[R_COUNT];
static volatile uint32_t last_resp_ms = 0;
static volatile bool discovered = false, probe_req = false, probe_busy = false;
static uint8_t sup[32];        // bit set = PID supported (0x01..0xFF)
static uint8_t map_src = 0x0B, frp_src = 0x23, pedal_src = 0x5A;
static volatile uint32_t rx_frames = 0, tx_err = 0;
static volatile bool rebuild = true;   // re-make the fast poll list

static void put(RawId id, float v) { raw_v[id] = v; raw_ms[id] = millis(); }
// Broadcast frames (not OBD answers). Called for every frame the OBD task reads.
static void bcast(const twai_message_t& m) {
  if (m.extd || m.rtr) return;
  if (m.identifier == BC_EB_ID && m.data_length_code >= 8) put(R_EB_HP, m.data[7] * BC_EB_HP_K);
}

bool obd_value(RawId id, float* out, uint32_t max_age) {
  uint32_t t = raw_ms[id];
  if (!t || millis() - t > max_age) return false;
  *out = raw_v[id]; return true;
}
bool obd_can_ok() { return last_resp_ms && millis() - last_resp_ms < 2000; }
bool obd_discovered() { return discovered; }
bool obd_supported(uint8_t pid) { return pid && (sup[pid >> 3] >> (7 - (pid & 7))) & 1; }
bool obd_ebp_supported() { return obd_supported(0x73); }
void obd_request_probe() { probe_req = true; }
bool obd_probe_busy() { return probe_req || probe_busy; }
uint32_t obd_rx_frames() { return rx_frames; }
uint32_t obd_tx_errors() { return tx_err; }

// ---------------------------------------------------------------- CAN I/O
static bool can_send(uint32_t id, const uint8_t* d) {
  twai_message_t m = {};
  m.identifier = id; m.data_length_code = 8;
  memcpy(m.data, d, 8);
  if (twai_transmit(&m, pdMS_TO_TICKS(5)) != ESP_OK) {
    tx_err = tx_err + 1;
    twai_clear_transmit_queue();
    return false;
  }
  return true;
}

// Send Mode 01 request, collect the ECM answer (single or multi-frame).
// out = [41 PID data...], returns length or 0. If list_ids, prints every responder.
static int query(uint8_t mode, uint8_t pid, uint8_t* out, int out_max, bool list_ids = false) {
  twai_message_t m;
  while (twai_receive(&m, 0) == ESP_OK) bcast(m);   // flush (keep broadcast values)
  uint8_t req[8] = {0x02, mode, pid, 0, 0, 0, 0, 0};
  if (!can_send(OBD_REQ_ID, req)) return 0;
  uint32_t t0 = millis(), deadline = t0 + OBD_TIMEOUT_MS;
  int total = 0, got = 0; uint8_t next_sn = 1; bool mf = false;
  while ((int32_t)(millis() - deadline) < 0) {
    if (twai_receive(&m, pdMS_TO_TICKS(5)) != ESP_OK) continue;
    rx_frames = rx_frames + 1;
    bcast(m);
    if (m.extd || m.identifier < 0x7E8 || m.identifier > 0x7EF) continue;
    uint8_t pci = m.data[0] >> 4;
    if (list_ids && LOG && pci <= 1) {
      LOG->printf("    responder 0x%03lX  frame:", (unsigned long)m.identifier);
      for (int i = 0; i < 8; i++) LOG->printf(" %02X", m.data[i]);
      LOG->println();
    }
    if (m.identifier != OBD_ECM_RESP) continue;
    if (pci == 0) {                                  // single frame
      int len = m.data[0] & 0x0F; if (len > 7 || len > out_max) continue;
      if (m.data[1] != mode + 0x40 || m.data[2] != pid) continue;
      memcpy(out, &m.data[1], len); last_resp_ms = millis();
      if (list_ids) { delay(40); while (twai_receive(&m, pdMS_TO_TICKS(5)) == ESP_OK) bcast(m); }
      return len;
    } else if (pci == 1) {                           // first frame
      total = ((m.data[0] & 0x0F) << 8) | m.data[1];
      if (total > out_max || m.data[2] != mode + 0x40 || m.data[3] != pid) continue;
      memcpy(out, &m.data[2], 6); got = 6; next_sn = 1; mf = true;
      uint8_t fc[8] = {0x30, 0x00, 0x00, 0, 0, 0, 0, 0}; // clear to send, no block limit, 0 ms
      can_send(OBD_ECM_PHYS, fc);
      deadline = millis() + OBD_MF_TIMEOUT_MS;
    } else if (pci == 2 && mf) {                     // consecutive frame
      if ((m.data[0] & 0x0F) != next_sn) { mf = false; continue; }
      next_sn = (next_sn + 1) & 0x0F;
      int n = total - got; if (n > 7) n = 7;
      memcpy(out + got, &m.data[1], n); got += n;
      if (got >= total) { last_resp_ms = millis(); return total; }
    }
  }
  return 0;
}

// ---------------------------------------------------------------- decode
static float baro_or_default() { float b; return obd_value(R_BARO_KPA, &b, 10000) ? b : 101.3f; }
static void decode(uint8_t pid, const uint8_t* d, int n) {   // d = data after [41 PID]
  float A = n > 0 ? d[0] : 0, B = n > 1 ? d[1] : 0;
  auto u16 = [&](int i) { return (float)((d[i] << 8) | d[i + 1]); };
  switch (pid) {
    case 0x04: put(R_LOAD, A * 100.f / 255.f); break;
    case 0x0B: if (map_src == 0x0B) put(R_MAP_KPA, A); break;
    case 0x0C: if (n >= 2) put(R_RPM, u16(0) / 4.f); break;
    case 0x0F: put(R_IAT_C, A - 40.f); break;
    case 0x23: if (frp_src == 0x23 && n >= 2) put(R_FRP_ACT_KPA, 10.f * u16(0)); break;
    case 0x33: put(R_BARO_KPA, A); break;
    case 0x42: if (n >= 2) put(R_VOLT, u16(0) / 1000.f); break;
    case 0x49: if (pedal_src == 0x49) put(R_PEDAL, A * 100.f / 255.f); break;
    case 0x59: if (frp_src == 0x59 && n >= 2) put(R_FRP_ACT_KPA, 10.f * u16(0) - baro_or_default()); break;
    case 0x5A: if (pedal_src == 0x5A) put(R_PEDAL, A * 100.f / 255.f); break;
    case 0x6D:
      if (n >= 5) {
        if (d[0] & 0x01) put(R_FRP_CMD_KPA, 10.f * u16(1));
        if ((d[0] & 0x02) && frp_src == 0x6D) put(R_FRP_ACT_KPA, 10.f * u16(3));
        if (!(d[0] & 0x02) && frp_src == 0x6D) { frp_src = obd_supported(0x23) ? 0x23 : 0x59; rebuild = true; }
      } break;
    case 0x70: if (map_src == 0x70 && n >= 5 && (d[0] & 0x02)) put(R_MAP_KPA, u16(3) * 0.03125f); break;
    case 0x71:
      if (n >= 3) { if (d[0] & 0x01) put(R_VGT_CMD, d[1] * 100.f / 255.f); if (d[0] & 0x02) put(R_VGT_ACT, d[2] * 100.f / 255.f); }
      break;
    case 0x73: if (n >= 3 && (d[0] & 0x01)) put(R_EXH_KPA, u16(1) * 0.01f); break;
    case 0x87:
      if (map_src == 0x87 && n >= 3) {
        if (d[0] & 0x01) put(R_MAP_KPA, u16(1) / 32.f);
        else { map_src = obd_supported(0x70) ? 0x70 : 0x0B; rebuild = true; }
      } break;
  }
  (void)B;
}

static bool discover(bool verbose) {
  memset(sup, 0, sizeof sup);
  bool any = false;
  const uint8_t ranges[6] = {0x00, 0x20, 0x40, 0x60, 0x80, 0xA0};
  for (int r = 0; r < 6; r++) {
    uint8_t b[16];
    int n = query(0x01, ranges[r], b, sizeof b, verbose && r == 0);
    if (verbose && LOG) {
      if (n >= 6) LOG->printf("  bitmap 0x%02X: %02X %02X %02X %02X\n", ranges[r], b[2], b[3], b[4], b[5]);
      else LOG->printf("  bitmap 0x%02X: no answer\n", ranges[r]);
    }
    if (n >= 6) { any = true; memcpy(&sup[ranges[r] >> 3], &b[2], 4); }
    // keep querying even if the "next range" bit is clear: some ECMs are inconsistent
  }
  if (!any) return false;
  map_src   = obd_supported(0x87) ? 0x87 : obd_supported(0x70) ? 0x70 : 0x0B;
  frp_src   = obd_supported(0x6D) ? 0x6D : obd_supported(0x23) ? 0x23 : 0x59;
  pedal_src = obd_supported(0x5A) ? 0x5A : 0x49;
  return true;
}

static bool probe() {
  probe_busy = true;
  Print& o = *LOG;
  o.println(F("\n===== PID PROBE (Mode 01, 0x7DF -> 0x7E8) ====="));
  if (!discover(true)) {
    o.println(F("  NO ANSWER from ECM. Check: key ON, CAN-H/CAN-L at OBD pins 6/14, GND pin 4/5,"));
    o.println(F("  SN65HVD230 3.3 V, TX/RX pins, 500 kbps. Then type 'probe' again."));
    o.println(F("==============================================="));
    probe_busy = false; return false;
  }
  o.println(F("  PID  SUP  NAME                                   FEEDS                  RAW DATA"));
  for (int i = 0; i < PID_TABLE_N; i++) {
    const PidDef& p = PID_TABLE[i];
    if (p.poll == POLL_ONCE) continue;
    bool s = obd_supported(p.pid);
    o.printf("  %02X   %-3s  %-38s %-22s", p.pid, s ? "YES" : "no", p.name, p.feeds);
    if (s) {
      uint8_t b[64]; int n = query(0x01, p.pid, b, sizeof b);
      if (n >= 2) { for (int k = 2; k < n; k++) o.printf(" %02X", b[k]); decode(p.pid, b + 2, n - 2); }
      else o.print(F(" (no answer)"));
    }
    o.println();
  }
  o.printf("  Sources chosen: BOOST/MAP=0x%02X  RAIL ACT=0x%02X  PEDAL=0x%02X\n", map_src, frp_src, pedal_src);
  if (map_src == 0x0B) o.println(F("  NOTE: MAP from 0x0B caps at 255 kPa (~22 psi boost). Boost above that will read flat."));
  o.printf("  EBP   (0x73): %s\n", obd_supported(0x73) ? "Mode 01 OK" : "NOT in Mode 01 -> needs Cummins Mode 22 (TODO in obd_pids.h)");
  o.printf("  VGT   (0x71): %s\n", obd_supported(0x71) ? "Mode 01 OK" : "NOT in Mode 01 -> needs Mode 22 (TODO)");
  o.printf("  RAIL CMD (0x6D): %s\n", obd_supported(0x6D) ? "Mode 01 OK" : "NOT in Mode 01 -> needs Mode 22 (TODO)");
  o.println(F("  Copy this whole report and keep it with the project notes."));
  o.println(F("==============================================="));
  probe_busy = false;
  return true;
}

// ---------------------------------------------------------------- task
static void obd_task(void*) {
  uint8_t fast[12]; int nfast = 0;
  const uint8_t slow[] = {0x33, 0x0F, 0x42, 0x0C};
  int slow_i = 0;
  uint32_t next_discover = 0;
  for (;;) {
    uint32_t al;
    if (twai_read_alerts(&al, 0) == ESP_OK) {
      if (al & TWAI_ALERT_BUS_OFF) twai_initiate_recovery();
      if (al & TWAI_ALERT_BUS_RECOVERED) twai_start();
    }
    if (probe_req) { probe_req = false; discovered = probe(); rebuild = true; continue; }
    if (!discovered) {
      if (millis() < next_discover) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
      if (discover(false)) { discovered = true; rebuild = true; } else { next_discover = millis() + 3000; continue; }
    }
    if (rebuild) {      // build the fast poll list from what the ECM supports
      rebuild = false; nfast = 0;
      // v2: VGT (0x71) no longer polled (page removed); 0x0C rpm added for the RAIL LOW "engine running" check
      const uint8_t want[] = {map_src, 0x73, pedal_src, frp_src, (uint8_t)(frp_src == 0x6D ? 0 : 0x6D), 0x04, 0x0C};
      for (uint8_t p : want) if (obd_supported(p)) fast[nfast++] = p;
      if (!nfast) fast[nfast++] = 0x04;
    }
    uint8_t b[64];
    for (int i = 0; i < nfast && !probe_req; i++) {
      int n = query(0x01, fast[i], b, sizeof b);
      if (n >= 2) decode(fast[i], b + 2, n - 2);
    }
    uint8_t sp = slow[slow_i++ % 4];
    if (obd_supported(sp)) { int n = query(0x01, sp, b, sizeof b); if (n >= 2) decode(sp, b + 2, n - 2); }
    twai_message_t bm;
    if (!obd_can_ok()) {                            // nobody answering: poll slowly, still read broadcasts
      uint32_t t0 = millis(); while (millis() - t0 < 500) { if (twai_receive(&bm, pdMS_TO_TICKS(20)) == ESP_OK) bcast(bm); }
    } else { while (twai_receive(&bm, 0) == ESP_OK) bcast(bm); vTaskDelay(pdMS_TO_TICKS(2)); }
  }
}

void obd_begin(int tx, int rx, Print* log) {
  LOG = log;
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)tx, (gpio_num_t)rx, TWAI_MODE_NORMAL);
  g.tx_queue_len = 4; g.rx_queue_len = 64;   // accept-all: the bus carries ~1,860 frames/s
  g.alerts_enabled = TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED | TWAI_ALERT_ERR_PASS;
  twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
  // v2: accept all frames. OBD answers (0x7E8..0x7EF) are picked in query(); broadcast 0x30A in bcast().
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g, &t, &f) != ESP_OK || twai_start() != ESP_OK) {
    if (LOG) LOG->println(F("TWAI (CAN) start FAILED"));
    return;
  }
  xTaskCreatePinnedToCore(obd_task, "obd", 6144, nullptr, 5, nullptr, 0);
}
