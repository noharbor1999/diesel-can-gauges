// =====================================================================================
//  OBD2 SNIFF-EVERYTHING  -  SNIFFER NODE  (ESP32-S3 + SN65HVD230)
//  2017 Ram 3500 6.7 Cummins.  One node per CAN pair found on the OBD2 port.
//
//  *** LISTEN-ONLY. THIS FIRMWARE NEVER TRANSMITS ON THE TRUCK BUS. ***
//  - The TWAI controller runs in TWAI_MODE_LISTEN_ONLY: it does not send frames,
//    does not send ACK bits, and does not send error frames.
//  - The TWAI TX queue length is 0 and twai_transmit() is never called anywhere.
//  - Extra hardware safety (recommended): wire the SN65HVD230 CTX/TX pin to 3V3 instead
//    of GPIO4. Then the transceiver physically cannot drive the bus (always recessive).
//  - Remove/cut the 120 ohm termination resistor on the SN65HVD230 module before
//    connecting to the truck. The truck bus is already terminated (about 60 ohm).
//
//  Same binary for every node. Set node ID / speed / fake mode over USB serial
//  (saved in flash):  n0..n4  b0 (auto) b500 b250 b125 b83 b33  f1/f0  h = help
//
//  LED (onboard RGB, always WHITE - no colors used): blink count every 2.5 s
//    1 = looking for bus speed / bus silent    2 = bus OK, no gateway heard
//    3 = streaming OK                          4 = frames dropped (last 5 s)
//    5 = bus errors (wrong speed or wiring)    6 = fake-bus bench mode streaming
//    slow long on/off (1 s / 1 s) = recording to PSRAM (BOOT button toggles)
// =====================================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <esp_timer.h>
#include <Preferences.h>
#include "driver/twai.h"
#include "sniff_proto.h"
#include "gvret_port.h"

// ----------------------------- USER CONFIG -----------------------------------------
#ifndef DEFAULT_NODE_ID
#define DEFAULT_NODE_ID   0        // 0..4. Overridden by the n<digit> serial command (saved).
#endif
#ifndef DEFAULT_BAUD
#define DEFAULT_BAUD      0        // 0 = auto-detect. Or 500000/250000/125000/83333/33333.
#endif
#ifndef FAKE_BUS
#define FAKE_BUS          0        // 1 = build defaults to fake-bus bench mode (no truck needed)
#endif
#define PIN_CAN_TX        4        // -> SN65HVD230 CTX (or tie CTX to 3V3, see above)
#define PIN_CAN_RX        5        // <- SN65HVD230 CRX
// XIAO ESP32-S3 settings baked in for Arduino IDE
#define PIN_LED 21
#define LED_IS_RGB 0
#define LED_ACTIVE_LOW 1
#ifndef PIN_LED
#define PIN_LED           48       // onboard WS2812 on most ESP32-S3 DevKitC-1 clones (some: 38)
#endif
#ifndef LED_IS_RGB
#define LED_IS_RGB        1        // 0 = plain LED on PIN_LED
#endif
#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW    0        // 1 = plain LED lights when pin is LOW (XIAO ESP32-S3 GPIO21)
#endif
#define PIN_BUTTON        0        // BOOT button: start/stop PSRAM recording
#define UART_BAUD         1000000  // COM (UART) port speed. SavvyCAN opens GVRET at 1,000,000.
#define FLUSH_MS          4        // max time a frame waits before its packet is sent
#define INFLIGHT_MAX      6        // ESP-NOW packets in flight at once
#define FRAME_QUEUE_LEN   3072     // node RAM queue between TWAI and ESP-NOW (frames)
// -------------------------------------------------------------------------------------

struct CFrame {             // compact frame, 20 bytes
  uint32_t t_us;
  uint32_t id;              // SNIFF_ID_EXT / SNIFF_ID_RTR flags in the top bits
  uint8_t  dlc;
  uint8_t  pad[3];
  uint8_t  data[8];
};

static const uint32_t BAUDS[] = {500000, 250000, 125000, 83333, 33333};

// ---- settings (NVS) ----
static Preferences prefs;
static uint8_t  nodeId = DEFAULT_NODE_ID;
static uint32_t baudSetting = DEFAULT_BAUD;   // 0 = auto
static bool     fakeMode = FAKE_BUS;
static uint32_t fakeExtraFps = 0;             // extra filler frames/s in fake mode (stress test)

// ---- runtime ----
static QueueHandle_t     frameQ;
static SemaphoreHandle_t inflightSem;
static SemaphoreHandle_t outLock;
static volatile uint32_t curBaud = 0;
static volatile uint8_t  nodeState = NODE_ST_SEARCHING;
static volatile bool     gwKnown = false, gwPeerAdded = false;
static uint8_t           gwMac[6];
static volatile uint32_t gwLastMs = 0;

static volatile uint32_t st_rxFrames = 0, st_busErr = 0, st_rxMissed = 0, st_rxOverrun = 0;
static volatile uint32_t st_qDrops = 0, st_txFail = 0, st_txOk = 0, st_noGw = 0;
static volatile uint8_t  st_tec = 0, st_rec = 0;
static volatile uint16_t st_fps = 0;
static volatile uint32_t lastDropMs = 0, lastBusErrMs = 0;
static volatile bool     twaiRunning = false;
static volatile bool     redetectRequest = false;
static volatile bool     verbose = false;

static uint16_t txSeq = 0;
static uint32_t frameIdx = 0;

// ---- PSRAM recording ----
static CFrame*  rec = nullptr;
static size_t   recCap = 0;
static volatile size_t recCount = 0;
static volatile bool   recording = false;
static uint32_t recStartUs = 0;

// ---- USB / UART ports ----
static GvretPort gvUsb, gvUart;
static String lineUsb, lineUart;

// =====================================================================================
//  Output helpers (text goes to every port that is NOT in GVRET binary mode)
// =====================================================================================
static void textOut(const char* s) {
  xSemaphoreTake(outLock, portMAX_DELAY);
  if (!gvUsb.binary)  Serial.print(s);
  if (!gvUart.binary) Serial0.print(s);
  xSemaphoreGive(outLock);
}
static void textf(const char* fmt, ...) {
  char b[256];
  va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
  textOut(b);
}

// =====================================================================================
//  LED - blink COUNT patterns, white only
// =====================================================================================
static void ledSet(bool on) {
#if LED_IS_RGB
  rgbLedWrite(PIN_LED, on ? 40 : 0, on ? 40 : 0, on ? 40 : 0);
#else
  digitalWrite(PIN_LED, (on ^ LED_ACTIVE_LOW) ? HIGH : LOW);
#endif
}
static void ledTask(void*) {
  for (;;) {
    if (recording) { ledSet(true); vTaskDelay(1000); ledSet(false); vTaskDelay(1000); continue; }
    uint8_t n = nodeState;
    for (uint8_t i = 0; i < n; i++) { ledSet(true); vTaskDelay(150); ledSet(false); vTaskDelay(250); }
    vTaskDelay(1500);
  }
}

// =====================================================================================
//  TWAI (listen-only) setup and auto-baud
// =====================================================================================
static twai_timing_config_t timingFor(uint32_t baud) {
  twai_timing_config_t t;
  switch (baud) {
    case 500000: { twai_timing_config_t x = TWAI_TIMING_CONFIG_500KBITS(); t = x; } break;
    case 250000: { twai_timing_config_t x = TWAI_TIMING_CONFIG_250KBITS(); t = x; } break;
    case 125000: { twai_timing_config_t x = TWAI_TIMING_CONFIG_125KBITS(); t = x; } break;
    default: {
      // 83.333k and 33.333k have no IDF macro. 80 MHz APB, 20 time quanta, 80% sample point.
      memset(&t, 0, sizeof(t));
      t.clk_src = TWAI_CLK_SRC_DEFAULT;
      t.quanta_resolution_hz = 0;                // 0 -> use brp below
      t.brp = (baud == 33333) ? 120 : 48;        // 80e6/(120*20)=33333, 80e6/(48*20)=83333
      t.tseg_1 = 15; t.tseg_2 = 4; t.sjw = 3; t.triple_sampling = false;
    } break;
  }
  return t;
}

static void twaiStop() {
  if (!twaiRunning) return;
  twai_stop();
  twai_driver_uninstall();
  twaiRunning = false;
}

static bool twaiStartListenOnly(uint32_t baud) {
  twaiStop();
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)PIN_CAN_TX, (gpio_num_t)PIN_CAN_RX,
                                                        TWAI_MODE_LISTEN_ONLY);   // <-- NEVER transmits/ACKs
  g.tx_queue_len = 0;                       // no TX queue at all
  g.rx_queue_len = 512;
  g.alerts_enabled = TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_RX_FIFO_OVERRUN |
                     TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_OFF;
  twai_timing_config_t t = timingFor(baud);
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g, &t, &f) != ESP_OK) { textf("TWAI install failed @%lu\n", (unsigned long)baud); return false; }
  if (twai_start() != ESP_OK) { twai_driver_uninstall(); textf("TWAI start failed\n"); return false; }
  twaiRunning = true;
  return true;
}

// Try one speed in listen-only mode. Accept when frames arrive with no bus errors.
static bool tryBaud(uint32_t baud) {
  if (!twaiStartListenOnly(baud)) return false;
  uint32_t windowMs = (baud >= 250000) ? 700 : (baud == 125000 ? 1200 : 1800);
  twai_status_info_t s0; twai_get_status_info(&s0);
  uint32_t frames = 0, start = millis();
  twai_message_t m;
  while (millis() - start < windowMs) {
    if (twai_receive(&m, pdMS_TO_TICKS(20)) == ESP_OK) frames++;
  }
  twai_status_info_t s1; twai_get_status_info(&s1);
  uint32_t errs = s1.bus_error_count - s0.bus_error_count;
  textf("  try %6lu bit/s: %4lu frames, %4lu bus errors\n", (unsigned long)baud, (unsigned long)frames, (unsigned long)errs);
  bool ok = (frames >= 3 && errs == 0) || (frames >= 30 && errs * 50 <= frames);
  if (!ok) twaiStop();
  return ok;
}

static void detectBaud() {
  nodeState = NODE_ST_SEARCHING;
  curBaud = 0;
  if (baudSetting != 0) {
    textf("Manual speed %lu bit/s (listen-only)\n", (unsigned long)baudSetting);
    if (twaiStartListenOnly(baudSetting)) curBaud = baudSetting;
    return;
  }
  textf("Auto speed detect (listen-only): 500k 250k 125k 83.3k 33.3k\n");
  for (;;) {
    for (uint32_t b : BAUDS) {
      if (tryBaud(b)) { curBaud = b; textf("FOUND bus speed %lu bit/s\n", (unsigned long)b); return; }
      if (fakeMode) return;
    }
    textf("No speed found yet (bus asleep? key off? wrong pins?). Trying again.\n");
    vTaskDelay(300);
  }
}

// =====================================================================================
//  Frame intake (real bus or fake bus) -> queue (+ PSRAM recording)
// =====================================================================================
static inline void intake(const CFrame& f) {
  st_rxFrames++;
  if (recording) {
    if (recCount < recCap) rec[recCount++] = f;
    else { recording = false; }
  }
  if (xQueueSend(frameQ, &f, 0) != pdTRUE) { st_qDrops++; lastDropMs = millis(); }
}

static void rxTask(void*) {
  uint32_t lastStat = millis(), lastFrames = 0, lastErr = 0, errAtFrameCheck = 0, framesAtCheck = 0;
  uint32_t checkMs = millis();
  for (;;) {
    if (fakeMode) { vTaskDelay(100); continue; }
    if (!twaiRunning || redetectRequest) { redetectRequest = false; detectBaud(); lastErr = 0; continue; }
    twai_message_t m;
    if (twai_receive(&m, pdMS_TO_TICKS(50)) == ESP_OK) {
      CFrame f;
      f.t_us = (uint32_t)esp_timer_get_time();
      f.id = m.identifier | (m.extd ? SNIFF_ID_EXT : 0) | (m.rtr ? SNIFF_ID_RTR : 0);
      f.dlc = m.data_length_code > 8 ? 8 : m.data_length_code;
      memcpy(f.data, m.data, 8);
      intake(f);
    }
    uint32_t now = millis();
    if (now - lastStat >= 1000) {
      twai_status_info_t s;
      if (twai_get_status_info(&s) == ESP_OK) {
        if (s.bus_error_count != lastErr) lastBusErrMs = now;
        lastErr = s.bus_error_count;
        st_busErr = s.bus_error_count; st_rxMissed = s.rx_missed_count; st_rxOverrun = s.rx_overrun_count;
        st_tec = s.tx_error_counter; st_rec = s.rx_error_counter;
        if (s.rx_missed_count || s.rx_overrun_count) {
          static uint32_t lastMiss = 0;
          if (s.rx_missed_count + s.rx_overrun_count != lastMiss) lastDropMs = now;
          lastMiss = s.rx_missed_count + s.rx_overrun_count;
        }
        if (s.state == TWAI_STATE_BUS_OFF) twai_initiate_recovery();
        if (s.state == TWAI_STATE_STOPPED) twai_start();
      }
      uint32_t fr = st_rxFrames;
      st_fps = (uint16_t)min<uint32_t>(fr - lastFrames, 65535);
      lastFrames = fr; lastStat = now;
    }
    // Auto mode: if the last 4 s had many errors and no frames, the speed is probably wrong -> redetect
    if (baudSetting == 0 && now - checkMs >= 4000) {
      uint32_t df = st_rxFrames - framesAtCheck, de = st_busErr - errAtFrameCheck;
      if (df == 0 && de > 20) { textf("Errors and no frames for 4 s -> re-detecting speed\n"); redetectRequest = true; }
      framesAtCheck = st_rxFrames; errAtFrameCheck = st_busErr; checkMs = now;
    }
  }
}

// ---- Fake bus: realistic-ish truck traffic so the whole chain can be tested on the bench ----
struct FakeId { uint16_t id; uint16_t periodMs; uint8_t dlc; uint8_t kind; uint32_t nextMs; uint8_t ctr; };
static FakeId fakeIds[64];
static int fakeCount = 0;

static void fakeSetup() {
  fakeCount = 0;
  auto add = [](uint16_t id, uint16_t p, uint8_t dlc, uint8_t kind) {
    if (fakeCount < 64) fakeIds[fakeCount++] = {id, p, dlc, kind, 0, 0};
  };
  add(0x0F0, 10, 8, 1);    // "engine": RPM ramps
  add(0x100, 20, 8, 2);    // "speed"
  add(0x2A0, 100, 8, 3);   // "lights": bit0 toggles every 10 s, byte1 = dimmer ramp
  add(0x2B0, 100, 4, 4);   // "doors": a bit toggles every 15 s
  add(0x300, 1000, 8, 0);  // static
  static const uint16_t periods[] = {10, 20, 20, 50, 50, 100, 100, 100, 200, 500, 1000};
  for (int i = 0; i < 40; i++) add(0x400 + i * 7, periods[i % 11], 8, 5);   // counters + noise
}

static void fakeTask(void*) {
  fakeSetup();
  uint32_t fillerAcc = 0, lastUs = (uint32_t)esp_timer_get_time();
  for (;;) {
    if (!fakeMode) { vTaskDelay(200); continue; }
    curBaud = 500000;
    uint32_t now = millis();
    for (int i = 0; i < fakeCount; i++) {
      FakeId& z = fakeIds[i];
      if ((int32_t)(now - z.nextMs) < 0) continue;
      z.nextMs = now + z.periodMs;
      CFrame f = {};
      f.t_us = (uint32_t)esp_timer_get_time();
      f.id = z.id; f.dlc = z.dlc;
      z.ctr++;
      switch (z.kind) {
        case 1: { uint16_t rpm = 700 + (uint16_t)(1300.0f * (0.5f + 0.5f * sinf(now / 3000.0f))); f.data[0] = rpm >> 8; f.data[1] = rpm; f.data[7] = z.ctr & 0x0F; } break;
        case 2: { uint16_t sp = (now / 100) % 1200; f.data[2] = sp >> 8; f.data[3] = sp; f.data[7] = z.ctr; } break;
        case 3: f.data[0] = ((now / 10000) & 1); f.data[1] = (now / 200) & 0xFF; break;
        case 4: f.data[0] = ((now / 15000) & 1) << 2; break;
        case 5: f.data[0] = z.ctr; f.data[1] = z.id & 0xFF; f.data[5] = esp_random() & 0x03; break;
        default: f.data[0] = 0x55; break;
      }
      intake(f);
    }
    // stress filler (r<fps> command)
    uint32_t us = (uint32_t)esp_timer_get_time();
    if (fakeExtraFps) {
      fillerAcc += (uint64_t)(us - lastUs) * fakeExtraFps / 1000;   // milli-frames
      while (fillerAcc >= 1000) {
        fillerAcc -= 1000;
        CFrame f = {}; f.t_us = (uint32_t)esp_timer_get_time(); f.id = 0x6F0; f.dlc = 8;
        static uint32_t n = 0; n++; memcpy(f.data, &n, 4);
        intake(f);
      }
    }
    lastUs = us;
    vTaskDelay(1);
  }
}

// =====================================================================================
//  ESP-NOW
// =====================================================================================
static void onSent(const esp_now_send_info_t*, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) st_txOk++; else { st_txFail++; lastDropMs = millis(); }
  xSemaphoreGive(inflightSem);
}

static void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len < (int)sizeof(PktBeacon)) return;
  const PktBeacon* b = (const PktBeacon*)data;
  if (b->magic != SNIFF_MAGIC || b->type != PKT_BEACON || b->version != SNIFF_VERSION) return;
  if (!gwKnown || memcmp(gwMac, info->src_addr, 6) != 0) { memcpy(gwMac, info->src_addr, 6); gwPeerAdded = false; gwKnown = true; }
  gwLastMs = millis();
}

static void ensurePeer() {
  if (!gwKnown || gwPeerAdded) return;
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, gwMac, 6);
  p.channel = SNIFF_WIFI_CHANNEL; p.ifidx = WIFI_IF_STA; p.encrypt = false;
  if (esp_now_is_peer_exist(gwMac)) esp_now_del_peer(gwMac);
  if (esp_now_add_peer(&p) == ESP_OK) {
    esp_now_rate_config_t rc = {};
    rc.phymode = WIFI_PHY_MODE_11G; rc.rate = WIFI_PHY_RATE_24M; rc.ersu = false; rc.dcm = false;
    esp_now_set_peer_rate_config(gwMac, &rc);   // faster air rate = more packets/s (short range is fine in a cab)
    gwPeerAdded = true;
    textf("Gateway found %02X:%02X:%02X:%02X:%02X:%02X\n", gwMac[0], gwMac[1], gwMac[2], gwMac[3], gwMac[4], gwMac[5]);
  }
}

static bool sendPacket(uint8_t* buf, size_t len) {
  ensurePeer();
  if (!gwPeerAdded || millis() - gwLastMs > 3000) { st_noGw++; return false; }
  if (xSemaphoreTake(inflightSem, pdMS_TO_TICKS(30)) != pdTRUE) { st_txFail++; lastDropMs = millis(); return false; }
  if (esp_now_send(gwMac, buf, len) != ESP_OK) { xSemaphoreGive(inflightSem); st_txFail++; lastDropMs = millis(); return false; }
  return true;
}

// =====================================================================================
//  TX task: batch frames into packets, also feed GVRET if a node is plugged in directly
// =====================================================================================
static uint8_t pkt[SNIFF_PAYLOAD_MAX];
static size_t  pktPos = sizeof(PktHdr);
static uint8_t pktCount = 0;
static uint32_t pktBase = 0, pktFirstIdx = 0, pktStartMs = 0;

static void flushPacket() {
  if (pktCount == 0) return;
  PktHdr* h = (PktHdr*)pkt;
  h->magic = SNIFF_MAGIC; h->version = SNIFF_VERSION; h->type = PKT_FRAMES; h->node = nodeId;
  h->seq = txSeq++; h->count = pktCount; h->flags = fakeMode ? 1 : 0;
  h->t_base_us = pktBase; h->first_idx = pktFirstIdx;
  sendPacket(pkt, pktPos);
  pktPos = sizeof(PktHdr); pktCount = 0;
}

static void sendStats() {
  PktStats s = {};
  s.h.magic = SNIFF_MAGIC; s.h.version = SNIFF_VERSION; s.h.type = PKT_STATS; s.h.node = nodeId;
  s.h.seq = txSeq++; s.h.flags = fakeMode ? 1 : 0; s.h.first_idx = frameIdx;
  s.baud = curBaud; s.state = nodeState; s.auto_baud = baudSetting == 0;
  s.tec = st_tec; s.rec = st_rec; s.rx_frames = st_rxFrames; s.bus_errors = st_busErr;
  s.rx_missed = st_rxMissed; s.rx_overrun = st_rxOverrun; s.q_drops = st_qDrops;
  s.tx_fail = st_txFail; s.tx_ok = st_txOk; s.uptime_s = millis() / 1000; s.fps = st_fps;
  sendPacket((uint8_t*)&s, sizeof(s));
}

static void handleLine(String& line);
static void pollPort(Stream& s, GvretPort& gv, String& line) {
  while (s.available()) {
    int c = s.read();
    if (c < 0) break;
    if (gv.binary || c == 0xE7 || c == 0xF1) {
      xSemaphoreTake(outLock, portMAX_DELAY);
      bool was = gv.binary;
      gv.bus0Speed = curBaud; gv.numBuses = nodeId + 1;
      gv.feed((uint8_t)c);
      xSemaphoreGive(outLock);
      if (!was && gv.binary) { /* switched to SavvyCAN mode: stop text on this port */ }
      continue;
    }
    if (c == '\n' || c == '\r') { if (line.length()) { handleLine(line); line = ""; } }
    else if (line.length() < 40) line += (char)c;
  }
}

static void txTask(void*) {
  uint32_t lastStatsMs = 0;
  for (;;) {
    pollPort(Serial, gvUsb, lineUsb);
    pollPort(Serial0, gvUart, lineUart);
    CFrame f;
    // drain up to a burst of frames
    int n = 0;
    while (n < 64 && xQueueReceive(frameQ, &f, n == 0 ? pdMS_TO_TICKS(2) : 0) == pdTRUE) {
      n++;
      uint32_t dt = f.t_us - pktBase;
      if (pktCount && (dt > 65000 || pktPos + SNIFF_REC_HDR + f.dlc > SNIFF_PAYLOAD_MAX || pktCount == 255)) flushPacket();
      if (pktCount == 0) { pktBase = f.t_us; pktFirstIdx = frameIdx; pktStartMs = millis(); dt = 0; }
      uint8_t* p = pkt + pktPos;
      p[0] = dt; p[1] = dt >> 8;
      memcpy(p + 2, &f.id, 4);
      p[6] = f.dlc;
      memcpy(p + 7, f.data, f.dlc);
      pktPos += SNIFF_REC_HDR + f.dlc; pktCount++; frameIdx++;
      // direct-USB fallback: a node plugged into SavvyCAN by itself works too
      if (gvUsb.binary || gvUart.binary) {
        xSemaphoreTake(outLock, portMAX_DELAY);
        bool ext = f.id & SNIFF_ID_EXT; uint32_t id = f.id & 0x1FFFFFFF;
        if (gvUsb.binary)  gvUsb.sendFrame(f.t_us, id, ext, nodeId, f.dlc, f.data);
        if (gvUart.binary) gvUart.sendFrame(f.t_us, id, ext, nodeId, f.dlc, f.data);
        xSemaphoreGive(outLock);
      }
      if (verbose) {
        char b[96]; int k = snprintf(b, sizeof(b), "%10lu %08lX [%u]", (unsigned long)f.t_us, (unsigned long)(f.id & 0x1FFFFFFF), f.dlc);
        for (int i = 0; i < f.dlc; i++) k += snprintf(b + k, sizeof(b) - k, " %02X", f.data[i]);
        snprintf(b + k, sizeof(b) - k, "\n"); textOut(b);
      }
    }
    if (pktCount && (millis() - pktStartMs >= FLUSH_MS || pktPos + SNIFF_REC_HDR + 8 > SNIFF_PAYLOAD_MAX)) flushPacket();
    if (millis() - lastStatsMs >= 1000) { lastStatsMs = millis(); sendStats(); }

    // state for LED / gateway
    uint32_t now = millis();
    uint8_t s;
    if (fakeMode) s = (gwPeerAdded && now - gwLastMs < 3000) ? NODE_ST_FAKE : NODE_ST_NO_GATEWAY;
    else if (curBaud == 0) s = NODE_ST_SEARCHING;
    else if (now - lastBusErrMs < 5000 && lastBusErrMs) s = NODE_ST_BUS_ERRORS;
    else if (!(gwPeerAdded && now - gwLastMs < 3000)) s = NODE_ST_NO_GATEWAY;
    else if (lastDropMs && now - lastDropMs < 5000) s = NODE_ST_DROPPING;
    else s = NODE_ST_OK;
    nodeState = s;
  }
}

// =====================================================================================
//  Serial commands
// =====================================================================================
static void printHelp() {
  textf("\n=== OBD2 sniffer node %u (LISTEN-ONLY, never transmits) ===\n", nodeId);
  textf("h        help              s   stats\n");
  textf("n0..n4   set node id (= SavvyCAN bus number), saves + reboots\n");
  textf("b0       auto speed        b500 b250 b125 b83 b33  manual speed (saves)\n");
  textf("f1 / f0  fake-bus bench mode on / off (saves + reboots)\n");
  textf("r<n>     fake mode: add n filler frames/s (stress test), e.g. r3000\n");
  textf("c        start/stop PSRAM recording (same as BOOT button)\n");
  textf("d        dump recording as SavvyCAN CSV     x  clear recording\n");
  textf("v        toggle printing every frame as text\n\n");
}

static void printStats() {
  textf("node %u  mode %s  speed %lu (%s)  state %u  gateway %s\n", nodeId, fakeMode ? "FAKE" : "TRUCK",
        (unsigned long)curBaud, baudSetting ? "manual" : "auto", nodeState,
        (gwPeerAdded && millis() - gwLastMs < 3000) ? "OK" : "not heard");
  textf("  frames %lu  fps %u  bus_err %lu  rx_missed %lu  overrun %lu  q_drops %lu\n",
        (unsigned long)st_rxFrames, st_fps, (unsigned long)st_busErr, (unsigned long)st_rxMissed,
        (unsigned long)st_rxOverrun, (unsigned long)st_qDrops);
  textf("  espnow ok %lu fail %lu no_gw %lu  tec %u rec %u  rec_buf %u/%u frames%s\n",
        (unsigned long)st_txOk, (unsigned long)st_txFail, (unsigned long)st_noGw, st_tec, st_rec,
        (unsigned)recCount, (unsigned)recCap, recording ? " RECORDING" : "");
}

static void toggleRecording() {
  if (!rec) { textf("No PSRAM buffer (PSRAM disabled in build?)\n"); return; }
  if (!recording) { recCount = 0; recStartUs = (uint32_t)esp_timer_get_time(); recording = true; textf("RECORDING to PSRAM (max %u frames)\n", (unsigned)recCap); }
  else { recording = false; textf("Recording stopped: %u frames. Type d to dump.\n", (unsigned)recCount); }
}

static void dumpRecording() {
  bool was = recording; recording = false;
  textf("Time Stamp,ID,Extended,Dir,Bus,LEN,D1,D2,D3,D4,D5,D6,D7,D8\n");
  for (size_t i = 0; i < recCount; i++) {
    const CFrame& f = rec[i];
    char b[96];
    bool ext = f.id & SNIFF_ID_EXT;
    int k = snprintf(b, sizeof(b), "%lu,%08lX,%s,Rx,%u,%u", (unsigned long)(f.t_us - recStartUs),
                     (unsigned long)(f.id & 0x1FFFFFFF), ext ? "true" : "false", nodeId, f.dlc);
    for (int j = 0; j < f.dlc; j++) k += snprintf(b + k, sizeof(b) - k, ",%02X", f.data[j]);
    snprintf(b + k, sizeof(b) - k, "\n");
    textOut(b);
    if ((i & 255) == 0) vTaskDelay(1);
  }
  textf("# end of dump, %u frames\n", (unsigned)recCount);
  (void)was;
}

static void handleLine(String& line) {
  line.trim();
  if (!line.length()) return;
  char c = line[0];
  long v = line.substring(1).toInt();
  switch (c) {
    case 'h': case '?': printHelp(); break;
    case 's': printStats(); break;
    case 'v': verbose = !verbose; break;
    case 'n': if (v >= 0 && v < SNIFF_MAX_NODES) { prefs.putUChar("node", v); textf("node id %ld saved, rebooting\n", v); delay(200); ESP.restart(); } break;
    case 'b': {
      uint32_t b = 0;
      if (v == 500) b = 500000; else if (v == 250) b = 250000; else if (v == 125) b = 125000;
      else if (v == 83) b = 83333; else if (v == 33) b = 33333; else if (v != 0) { textf("unknown speed\n"); break; }
      baudSetting = b; prefs.putULong("baud", b); textf("speed setting %s saved\n", b ? "manual" : "auto");
      redetectRequest = true;   // rxTask restarts TWAI itself
    } break;
    case 'f': prefs.putBool("fake", v == 1); textf("fake mode %s saved, rebooting\n", v == 1 ? "ON" : "OFF"); delay(200); ESP.restart(); break;
    case 'r': fakeExtraFps = v < 0 ? 0 : v; textf("fake filler %lu frames/s\n", (unsigned long)fakeExtraFps); break;
    case 'c': toggleRecording(); break;
    case 'd': dumpRecording(); break;
    case 'x': recCount = 0; textf("recording cleared\n"); break;
    default: textf("? (h = help)\n"); break;
  }
}

// =====================================================================================
void setup() {
  Serial.begin(115200);          // native USB CDC (baud ignored)
  Serial.setTxTimeoutMs(0);
  Serial0.begin(UART_BAUD);      // COM port via USB-UART chip
  outLock = xSemaphoreCreateMutex();
#if !LED_IS_RGB
  pinMode(PIN_LED, OUTPUT);
#endif
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  prefs.begin("sniff", false);
  nodeId = prefs.getUChar("node", DEFAULT_NODE_ID);
  if (nodeId >= SNIFF_MAX_NODES) nodeId = 0;
  baudSetting = prefs.getULong("baud", DEFAULT_BAUD);
  fakeMode = prefs.getBool("fake", FAKE_BUS);

  gvUsb.begin(&Serial, nodeId + 1);
  gvUart.begin(&Serial0, nodeId + 1);

  frameQ = xQueueCreate(FRAME_QUEUE_LEN, sizeof(CFrame));
  inflightSem = xSemaphoreCreateCounting(INFLIGHT_MAX, INFLIGHT_MAX);

  if (psramFound()) {
    size_t freeP = ESP.getMaxAllocPsram();
    if (freeP > 128 * 1024) {
      recCap = (freeP - 64 * 1024) / sizeof(CFrame);
      rec = (CFrame*)ps_malloc(recCap * sizeof(CFrame));
      if (!rec) recCap = 0;
    }
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_channel(SNIFF_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) textf("ESP-NOW init FAILED\n");
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onRecv);

  delay(300);
  printHelp();
  textf("MAC %s  channel %d  payload %d bytes  PSRAM record buffer %u frames\n",
        WiFi.macAddress().c_str(), SNIFF_WIFI_CHANNEL, SNIFF_PAYLOAD_MAX, (unsigned)recCap);

  xTaskCreatePinnedToCore(ledTask, "led", 2048, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(txTask, "tx", 6144, nullptr, 5, nullptr, 1);
  xTaskCreatePinnedToCore(fakeTask, "fake", 4096, nullptr, 6, nullptr, 1);
  xTaskCreatePinnedToCore(rxTask, "rx", 6144, nullptr, 10, nullptr, 1);
}

void loop() {
  static uint32_t pressMs = 0; static bool last = true;
  bool b = digitalRead(PIN_BUTTON);
  if (!b && last) pressMs = millis();
  if (b && !last && millis() - pressMs > 50) toggleRecording();
  last = b;
  delay(10);
}
