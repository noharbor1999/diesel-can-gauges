// ===== XIAO ESP32-C6 build (Nov 22 2026) v4: sniff everything + polite Mode 01 PID poller =====
// Arduino IDE: Board = XIAO_ESP32C6, USB CDC On Boot = Enabled. Everything else default.
// Wiring: 230 CTX -> D3 (GPIO21), 230 CRX -> D4 (GPIO22), 3V3, GND -> OBD pin 5. No 120 ohm terminator.
//   v4 TRANSMITS (read-only requests), so CTX must go to D3, NOT to 3V3 like the v3 listen-only setup.
// Marker button: momentary switch between pad D5 (GPIO23) and GND, internal pull-up.
//   D5 = GPIO23 (Seeed wiki + Arduino pins_arduino.h). Not a strapping pin (those are
//   GPIO8, GPIO9, GPIO15, MTMS, MTDI). Not the CAN pins (TX GPIO21 / RX GPIO22).
// BOOT (GPIO9) is a second marker input. No PSRAM, so BOOT is not used for recording.
// A press injects one extended frame ID 0x1FFFFFF0 into the GVRET stream (not onto the truck bus).
#define PIN_CAN_TX     21
#define PIN_CAN_RX     22
#define PIN_BUTTON     9       // BOOT: second marker input (active low, internal pull-up)
#define PIN_MARK       23      // D5: external marker button to GND (active low, internal pull-up)
#define PIN_LED        15
#define LED_IS_RGB     0
#define LED_ACTIVE_LOW 1
#define MARKER_CAN_ID  0x1FFFFFF0UL  // 29-bit extended; cannot collide with 11-bit Ram IDs (0..0x7FF)
#define MARKER_DEBOUNCE_MS 50
// =====================================================================================
//  OBD2 SNIFF-EVERYTHING  -  SNIFFER NODE v4  (XIAO ESP32-C6 + SN65HVD230)
//  2017 Ram 3500 6.7 Cummins, CAN-C (OBD pins 6/14), 500 kbit/s.
//
//  v4 = v3 sniffer (every bus frame -> GVRET/SavvyCAN, markers, ESP-NOW) PLUS a polite,
//  READ-ONLY OBD-II Mode 01 poller so requests and replies land in the same SavvyCAN log.
//
//  *** WHAT THIS FIRMWARE TRANSMITS ON THE TRUCK BUS (nothing else, ever) ***
//   - "02 01 <pid>" Mode 01 requests: 01 00 to 0x7DF (wake-up), poll list to 0x7E0 (ECM only).
//   - ISO-TP flow control "30 00 00" to 0x7E0, only after the ECM sends a First Frame on 0x7E8.
//   - No Mode 22 / 03 / 04 / 2E / 31 / 10, no session changes, no writes, no clears.
//   - SavvyCAN cannot make it transmit: GVRET "send frame" (cmd 0x00) is still read and discarded.
//   - TWAI runs in NORMAL mode at 500k (it ACKs frames, like any module). Accept-all filter.
//   - Every request is single-shot (no endless hardware retry if nobody ACKs). Up to 3 software tries.
//   - Before any wake-up probe the bus must already be awake (broadcast frames heard in the last 1 s).
//   - If another tester shows up (0x7DF/0x7E0 frames that are not ours), polling pauses 5 s.
//
//  Poller: 3 s after TWAI starts, 01 00 -> 0x7DF every 500 ms until 0x7E8 answers (max 15 s).
//  Then 6D 73 87 74 04 62 5E 0C to 0x7E0, one at a time (reply or 100 ms timeout), 31 ms slots
//  -> ~32 requests/s, ~4 Hz per PID, about 2 % bus load. 20 misses in a row (key off) -> stop,
//  retry the wake-up every 10 s. Own requests + flow control are echoed into the GVRET stream
//  as normal frames (GVRET has no TX direction flag; SavvyCAN marks every frame Rx).
//
//  Serial commands (text mode, before SavvyCAN connects): h help, s stats, q poll stats,
//  p0 / p1 poller off / on (saved), n0..n4 node id, f1/f0 fake bus, v verbose.
//
//  LED (onboard, plain): blink count every 2.5 s
//    1 = bus not up yet  2 = bus OK, no gateway heard (normal when on USB)  3 = streaming OK
//    4 = frames dropped (last 5 s)  5 = bus errors  6 = fake-bus bench mode
//    3 quick blinks = a capture MARKER was injected (D5 or BOOT)
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
// Single-core chips (C6/C3) only have core 0.
#if CONFIG_FREERTOS_UNICORE || (portNUM_PROCESSORS == 1)
#define CORE(n) 0
#else
#define CORE(n) (n)
#endif

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
#ifndef PIN_CAN_TX
#define PIN_CAN_TX        4        // -> SN65HVD230 CTX (or tie CTX to 3V3, see above)
#endif
#ifndef PIN_CAN_RX
#define PIN_CAN_RX        5        // <- SN65HVD230 CRX
#endif
#ifndef PIN_LED
#define PIN_LED           48       // onboard WS2812 on most ESP32-S3 DevKitC-1 clones (some: 38)
#endif
#ifndef LED_IS_RGB
#define LED_IS_RGB        1        // 0 = plain LED on PIN_LED
#endif
#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW    0        // 1 = plain LED lights when pin is LOW (XIAO ESP32-S3 GPIO21)
#endif
#ifndef PIN_BUTTON
#define PIN_BUTTON        0        // BOOT button: start/stop PSRAM recording (S3). On C6 it is a marker input.
#endif
#ifndef PIN_MARK
#define PIN_MARK          -1       // -1 = no extra marker pin (S3 default). C6 sets this to 23 (D5).
#endif
#ifndef MARKER_CAN_ID
#define MARKER_CAN_ID     0x1FFFFFF0UL
#endif
#ifndef MARKER_DEBOUNCE_MS
#define MARKER_DEBOUNCE_MS 50
#endif
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

// Debounced marker button (defined here so Arduino's auto-prototypes can see it).
struct MarkerBtn {
  int8_t   pin;
  bool     level;       // last raw level, true = released (pull-up)
  bool     armed;       // true when we may fire again
  uint32_t edgeMs;
};

// ---------------------------- v4 PID POLLER CONFIG ----------------------------------
#ifndef POLL_DEFAULT_ON
#define POLL_DEFAULT_ON       1        // p0 / p1 serial command overrides (saved)
#endif
#define POLL_BAUD             500000   // CAN-C. NORMAL mode, fixed (no auto-baud while able to ACK)
#define OBD_REQ_FUNC          0x7DF    // functional request (wake-up 01 00 only)
#define OBD_REQ_ECM           0x7E0    // ECM physical request (poll list + flow control)
#define OBD_RESP_ECM          0x7E8    // ECM response
#define POLL_START_DELAY_MS   3000     // after TWAI start
#define WAKE_PERIOD_MS        500
#define WAKE_MAX_MS           15000
#define WAKE_RETRY_MS         10000    // after a failed wake-up or after 20 misses
#define BUS_AWAKE_MS          1000     // a broadcast frame must have been heard this recently
#define POLL_REPLY_TIMEOUT_MS 100
#define POLL_SLOT_MS          31       // 8 PIDs x 31 ms = 248 ms -> ~4 Hz per PID, ~32 req/s
#define POLL_MIN_GAP_MS       2        // idle time after a reply before the next request
#define POLL_MAX_MISSES       20
#define TESTER_BACKOFF_MS     5000     // pause if another scan tool is talking
#define TX_TRIES              3        // single-shot tries (arbitration loss / no ACK)
#define TX_DONE_WAIT_MS       30

static const uint8_t POLL_PIDS[] = {0x6D, 0x73, 0x87, 0x74, 0x04, 0x62, 0x5E, 0x0C};
#define N_POLL ((int)sizeof(POLL_PIDS))

struct PollStat { uint32_t req, ok, neg, miss; uint8_t last[16]; uint8_t lastLen; };

enum : uint8_t { PS_OFF = 0, PS_START, PS_WAKE, PS_POLL, PS_RETRY };


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

// ---- v4 poller runtime ----
static QueueHandle_t     pollQ;                    // 0x7E8 frames copied from rxTask
static volatile bool     pollEnabled = POLL_DEFAULT_ON;
static volatile uint8_t  pollState = PS_OFF;
static volatile uint32_t twaiUpMs = 0;
static volatile uint32_t lastBusFrameMs = 0;       // any received frame
static volatile uint32_t lastForeignTesterMs = 0;  // 0x7DF/0x7E0 frames we did NOT send
static uint32_t st_foreignTester = 0, st_txEcho = 0, st_canTxFail = 0, st_isotpErr = 0;
static uint32_t st_pollQDrops = 0, st_wakeTries = 0, st_pollStops = 0;
static uint32_t          pollStartMs = 0;
static PollStat          pstat[N_POLL];

static uint16_t txSeq = 0;
static uint32_t frameIdx = 0;

// ---- PSRAM recording ----
static CFrame*  rec = nullptr;
static size_t   recCap = 0;
static volatile size_t recCount = 0;
static volatile bool   recording = false;
static uint32_t recStartUs = 0;

// ---- capture markers (C6: D5 / BOOT; injected as a fake Rx frame, never on the truck bus) ----
static uint8_t           markerNum = 0;          // 1,2,3... resets at boot; wraps 255 -> 1
static volatile uint8_t  ledMarkFlashes = 0;     // remaining on/off pairs for the 3-blink

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
    if (ledMarkFlashes) {
      while (ledMarkFlashes) {
        ledSet(true);  vTaskDelay(pdMS_TO_TICKS(70));
        ledSet(false); vTaskDelay(pdMS_TO_TICKS(70));
        if (ledMarkFlashes) ledMarkFlashes--;
      }
      continue;
    }
    if (recording) { ledSet(true); vTaskDelay(1000); ledSet(false); vTaskDelay(1000); continue; }
    uint8_t n = nodeState;
    for (uint8_t i = 0; i < n; i++) { ledSet(true); vTaskDelay(150); ledSet(false); vTaskDelay(250); }
    vTaskDelay(1500);
  }
}

// =====================================================================================
//  TWAI setup: NORMAL mode, 500k fixed, accept all frames
// =====================================================================================
static void twaiStop() {
  if (!twaiRunning) return;
  twai_stop();
  twai_driver_uninstall();
  twaiRunning = false;
}

static bool twaiStartNormal(uint32_t baud) {
  twaiStop();
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)PIN_CAN_TX, (gpio_num_t)PIN_CAN_RX,
                                                        TWAI_MODE_NORMAL);   // ACKs; TX only from pollTask
  g.tx_queue_len = 4;
  g.rx_queue_len = 512;
  g.alerts_enabled = TWAI_ALERT_TX_SUCCESS | TWAI_ALERT_TX_FAILED | TWAI_ALERT_BUS_ERROR |
                     TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_RX_FIFO_OVERRUN | TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_OFF;
  twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (baud != 500000) { textf("v4 supports 500k only\n"); return false; }
  if (twai_driver_install(&g, &t, &f) != ESP_OK) { textf("TWAI install failed @%lu\n", (unsigned long)baud); return false; }
  if (twai_start() != ESP_OK) { twai_driver_uninstall(); textf("TWAI start failed\n"); return false; }
  twaiRunning = true;
  twaiUpMs = millis();
  textf("TWAI NORMAL mode %lu bit/s, accept-all\n", (unsigned long)baud);
  return true;
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
    if (!twaiRunning || redetectRequest) {
      redetectRequest = false;
      if (twaiStartNormal(POLL_BAUD)) curBaud = POLL_BAUD; else { curBaud = 0; vTaskDelay(1000); }
      lastErr = 0; continue;
    }
    twai_message_t m;
    if (twai_receive(&m, pdMS_TO_TICKS(50)) == ESP_OK) {
      CFrame f;
      f.t_us = (uint32_t)esp_timer_get_time();
      f.id = m.identifier | (m.extd ? SNIFF_ID_EXT : 0) | (m.rtr ? SNIFF_ID_RTR : 0);
      f.dlc = m.data_length_code > 8 ? 8 : m.data_length_code;
      memcpy(f.data, m.data, 8);
      intake(f);
      lastBusFrameMs = millis();
      if (!m.extd && !m.rtr) {
        if (m.identifier == OBD_RESP_ECM) {
          if (xQueueSend(pollQ, &f, 0) != pdTRUE) st_pollQDrops++;
        } else if (m.identifier == OBD_REQ_FUNC || m.identifier == OBD_REQ_ECM) {
          // TWAI never receives its own TX, so these came from another tester.
          lastForeignTesterMs = millis(); st_foreignTester++;
        }
      }
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
    // v4: speed is fixed (500k NORMAL). Just report sustained errors-with-no-frames.
    if (now - checkMs >= 4000) {
      uint32_t df = st_rxFrames - framesAtCheck, de = st_busErr - errAtFrameCheck;
      if (df == 0 && de > 20) textf("Bus errors and no frames for 4 s: check wiring / pins 6-14 / key on\n");
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
//  v4 POLLER: read-only Mode 01 requests to the ECM, ISO-TP reassembly, echo into GVRET
// =====================================================================================
// Push one of OUR transmitted frames into the stream (TWAI does not receive its own TX).
static void echoTx(uint32_t id, const uint8_t d[8]) {
  CFrame f;
  memset(&f, 0, sizeof(f));
  f.t_us = (uint32_t)esp_timer_get_time();   // same clock as received frames, taken at TX done
  f.id = id;
  f.dlc = 8;
  memcpy(f.data, d, 8);
  if (xQueueSend(frameQ, &f, 0) != pdTRUE) { st_qDrops++; lastDropMs = millis(); }
  st_txEcho++;
}

static bool busRunning() {
  if (!twaiRunning) return false;
  twai_status_info_t s;
  return twai_get_status_info(&s) == ESP_OK && s.state == TWAI_STATE_RUNNING;
}

// Single-shot transmit with up to TX_TRIES tries. Echoed to GVRET only if it went out (ACKed).
static bool canTx(uint32_t id, const uint8_t d[8]) {
  twai_message_t m;
  memset(&m, 0, sizeof(m));
  m.identifier = id;
  m.data_length_code = 8;
  m.ss = 1;                                  // no endless hardware retransmit
  memcpy(m.data, d, 8);
  for (int t = 0; t < TX_TRIES; t++) {
    if (!busRunning()) return false;
    uint32_t a;
    while (twai_read_alerts(&a, 0) == ESP_OK) {}           // clear stale alert bits
    if (twai_transmit(&m, pdMS_TO_TICKS(10)) != ESP_OK) { st_canTxFail++; continue; }
    uint32_t t0 = millis();
    bool ok = false;
    while (millis() - t0 < TX_DONE_WAIT_MS) {
      if (twai_read_alerts(&a, pdMS_TO_TICKS(5)) != ESP_OK) continue;
      if (a & TWAI_ALERT_TX_SUCCESS) { ok = true; break; }
      if (a & TWAI_ALERT_TX_FAILED) break;
    }
    if (ok) { echoTx(id, d); return true; }
    st_canTxFail++;
  }
  return false;
}

static bool replyMatches(const uint8_t* p, uint16_t n, uint8_t pid) {
  if (n >= 2 && p[0] == 0x41 && p[1] == pid) return true;
  if (n >= 3 && p[0] == 0x7F && p[1] == 0x01) return true;    // negative response to Mode 01
  return false;
}

// Send 02 01 <pid> to dest, collect the ECM (0x7E8) reply.
// Returns 1 = positive reply, 2 = negative reply, 0 = no reply, -1 = could not transmit.
static int pollTransact(uint32_t dest, uint8_t pid, uint8_t* out, uint8_t& outLen) {
  outLen = 0;
  CFrame junk;
  while (xQueueReceive(pollQ, &junk, 0) == pdTRUE) {}       // drop stale replies
  uint8_t req[8] = {0x02, 0x01, pid, 0, 0, 0, 0, 0};
  if (!canTx(dest, req)) return -1;

  bool isoActive = false;
  uint16_t expected = 0, got = 0;
  uint8_t nextSn = 0;
  uint8_t buf[64];
  uint32_t deadline = millis() + POLL_REPLY_TIMEOUT_MS;
  while ((int32_t)(deadline - millis()) > 0) {
    CFrame f;
    if (xQueueReceive(pollQ, &f, pdMS_TO_TICKS(2)) != pdTRUE) continue;
    if (f.dlc < 1) continue;
    uint8_t pci = f.data[0] >> 4;
    if (pci == 0x0 && !isoActive) {                           // Single Frame
      uint8_t n = f.data[0] & 0x0F;
      if (n == 0 || n > 7 || !replyMatches(f.data + 1, n, pid)) continue;
      if (f.data[1] == 0x7F && n >= 3 && f.data[3] == 0x78) { deadline = millis() + 500; continue; }  // pending
      memcpy(out, f.data + 1, n); outLen = n;
      return f.data[1] == 0x7F ? 2 : 1;
    } else if (pci == 0x1 && !isoActive) {                    // First Frame -> flow control now
      expected = ((uint16_t)(f.data[0] & 0x0F) << 8) | f.data[1];
      if (expected < 8 || !replyMatches(f.data + 2, 6, pid)) continue;
      memcpy(buf, f.data + 2, 6); got = 6; nextSn = 1; isoActive = true;
      uint8_t fc[8] = {0x30, 0x00, 0x00, 0, 0, 0, 0, 0};     // CTS, BS=0, STmin=0
      if (!canTx(OBD_REQ_ECM, fc)) { st_isotpErr++; return 0; }
      deadline = millis() + POLL_REPLY_TIMEOUT_MS;
    } else if (pci == 0x2 && isoActive) {                     // Consecutive Frame
      if ((f.data[0] & 0x0F) != nextSn) { st_isotpErr++; return 0; }
      nextSn = (nextSn + 1) & 0x0F;
      uint16_t remain = expected - got;
      uint16_t chunk = remain < 7 ? remain : 7;
      if (got + chunk > sizeof(buf)) chunk = sizeof(buf) - got;
      memcpy(buf + got, f.data + 1, chunk); got += chunk;
      deadline = millis() + POLL_REPLY_TIMEOUT_MS;
      if (got >= expected || got >= sizeof(buf)) {
        uint8_t n = got > 16 ? 16 : (uint8_t)got;            // poll list replies are <= 13 bytes
        memcpy(out, buf, n); outLen = n;
        return buf[0] == 0x7F ? 2 : 1;
      }
    }
  }
  if (isoActive) st_isotpErr++;                               // transfer started but never finished
  return 0;
}

static bool busAwake() { return lastBusFrameMs && millis() - lastBusFrameMs < BUS_AWAKE_MS; }
static bool pollAllowed() { return pollEnabled && !fakeMode && twaiRunning; }

// 01 00 to 0x7DF every 500 ms until 0x7E8 answers (max 15 s). Never probes a silent bus.
static bool wakeUp() {
  pollState = PS_WAKE;
  textf("POLL wake-up: 01 00 -> 0x7DF every %d ms (max %d s)\n", WAKE_PERIOD_MS, WAKE_MAX_MS / 1000);
  uint32_t t0 = millis();
  uint8_t out[16], n;
  while (millis() - t0 < WAKE_MAX_MS) {
    if (!pollAllowed()) return false;
    uint32_t ts = millis();
    if (busAwake() && millis() - lastForeignTesterMs >= TESTER_BACKOFF_MS) {
      st_wakeTries++;
      if (pollTransact(OBD_REQ_FUNC, 0x00, out, n) == 1) {
        textf("POLL ECM answered after %lu ms -> polling\n", (unsigned long)(millis() - t0));
        return true;
      }
    }
    while (millis() - ts < WAKE_PERIOD_MS) vTaskDelay(pdMS_TO_TICKS(10));
  }
  textf("POLL no ECM answer in %d s (key off?). Retry in %d s\n", WAKE_MAX_MS / 1000, WAKE_RETRY_MS / 1000);
  return false;
}

static void pollTask(void*) {
  uint32_t retryAt = 0, lastSendMs = 0, upSeen = 0;
  int idx = 0, misses = 0;
  for (;;) {
    if (!pollAllowed()) { pollState = PS_OFF; upSeen = 0; vTaskDelay(pdMS_TO_TICKS(200)); continue; }
    if (upSeen != twaiUpMs) { upSeen = twaiUpMs; pollState = PS_START; }   // (re)started TWAI
    uint32_t now = millis();
    if (pollState == PS_OFF) pollState = PS_START;
    if (pollState == PS_START) {
      if (now - twaiUpMs < POLL_START_DELAY_MS) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
      pollState = PS_WAKE;
    }
    if (pollState == PS_WAKE) {
      if (wakeUp()) { pollState = PS_POLL; misses = 0; idx = 0; if (!pollStartMs) pollStartMs = millis(); }
      else if (pollAllowed()) { pollState = PS_RETRY; retryAt = millis() + WAKE_RETRY_MS; }
      continue;
    }
    if (pollState == PS_RETRY) {
      if ((int32_t)(millis() - retryAt) >= 0) pollState = PS_WAKE; else vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    // ---- PS_POLL ----
    if (millis() - lastForeignTesterMs < TESTER_BACKOFF_MS && lastForeignTesterMs) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
    while ((int32_t)(millis() - (lastSendMs + POLL_SLOT_MS)) < 0) vTaskDelay(1);
    uint8_t pid = POLL_PIDS[idx];
    PollStat& ps = pstat[idx];
    idx = (idx + 1) % N_POLL;
    lastSendMs = millis();
    uint8_t out[16], n = 0;
    int r = pollTransact(OBD_REQ_ECM, pid, out, n);
    if (r >= 0) ps.req++;
    if (r == 1 || r == 2) {
      misses = 0;
      if (r == 1) ps.ok++; else ps.neg++;
      memcpy(ps.last, out, n); ps.lastLen = n;
    } else {
      ps.miss++;
      if (++misses >= POLL_MAX_MISSES) {
        st_pollStops++;
        textf("POLL %d requests in a row unanswered -> stopped, wake-up retry every %d s\n", POLL_MAX_MISSES, WAKE_RETRY_MS / 1000);
        pollState = PS_RETRY; retryAt = millis() + WAKE_RETRY_MS; misses = 0;
        continue;
      }
    }
    uint32_t gapUntil = millis() + POLL_MIN_GAP_MS;
    while ((int32_t)(millis() - gapUntil) < 0) vTaskDelay(1);
  }
}

static void printPollStats() {
  static const char* SN[] = {"off", "start delay", "waking", "POLLING", "retry wait"};
  uint32_t el = pollStartMs ? millis() - pollStartMs : 0;
  textf("poller %s  state %s  wake tries %lu  stops %lu  tx echo %lu  tx fail %lu  isotp err %lu  pollQ drops %lu\n",
        pollEnabled ? "ON" : "OFF", SN[pollState < 5 ? pollState : 0], (unsigned long)st_wakeTries,
        (unsigned long)st_pollStops, (unsigned long)st_txEcho, (unsigned long)st_canTxFail,
        (unsigned long)st_isotpErr, (unsigned long)st_pollQDrops);
  textf("  other tester frames seen %lu (polling pauses %d s when one talks)\n", (unsigned long)st_foreignTester, TESTER_BACKOFF_MS / 1000);
  for (int i = 0; i < N_POLL; i++) {
    const PollStat& p = pstat[i];
    char b[160];
    int k = snprintf(b, sizeof(b), "  PID %02X  req %6lu ok %6lu neg %4lu miss %5lu  %5.2f Hz  last:", POLL_PIDS[i],
                     (unsigned long)p.req, (unsigned long)p.ok, (unsigned long)p.neg, (unsigned long)p.miss,
                     el ? p.ok * 1000.0f / el : 0.0f);
    for (int j = 0; j < p.lastLen && k < (int)sizeof(b) - 4; j++) k += snprintf(b + k, sizeof(b) - k, " %02X", p.last[j]);
    snprintf(b + k, sizeof(b) - k, "\n");
    textOut(b);
  }
}

// =====================================================================================
//  Serial commands
// =====================================================================================
static void printHelp() {
  textf("\n=== OBD2 sniffer node %u  v4 (500k NORMAL, read-only Mode 01 poller) ===\n", nodeId);
  textf("h        help              s   stats\n");
  textf("n0..n4   set node id (= SavvyCAN bus number), saves + reboots\n");
  textf("q        poller stats      p1 / p0  poller on / off (saves)\n");
  textf("         v4 speed is fixed at 500k (CAN-C, OBD pins 6/14). b<n> is ignored.\n");
  textf("f1 / f0  fake-bus bench mode on / off (saves + reboots)\n");
  textf("r<n>     fake mode: add n filler frames/s (stress test), e.g. r3000\n");
  textf("c        start/stop PSRAM recording (S3 BOOT; no PSRAM on the C6)\n");
  textf("d        dump recording as SavvyCAN CSV     x  clear recording\n");
  textf("v        toggle printing every frame as text\n");
  textf("C6 marker: click D5 (GPIO23, button to GND) or BOOT to mark the timeline\n");
  textf("         SavvyCAN shows an extended frame ID 1FFFFFF0 (never sent on the truck bus)\n\n");
}

static void printStats() {
  textf("node %u  mode %s  speed %lu (fixed, NORMAL)  state %u  gateway %s\n", nodeId, fakeMode ? "FAKE" : "TRUCK",
        (unsigned long)curBaud, nodeState,
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
    case 's': printStats(); printPollStats(); break;
    case 'v': verbose = !verbose; break;
    case 'n': if (v >= 0 && v < SNIFF_MAX_NODES) { prefs.putUChar("node", v); textf("node id %ld saved, rebooting\n", v); delay(200); ESP.restart(); } break;
    case 'b': textf("v4: speed fixed at 500k NORMAL mode (b ignored)\n"); break;
    case 'p': pollEnabled = (v == 1); prefs.putBool("poll", pollEnabled); textf("poller %s (saved)\n", pollEnabled ? "ON" : "OFF"); break;
    case 'q': printPollStats(); break;
    case 'f': prefs.putBool("fake", v == 1); textf("fake mode %s saved, rebooting\n", v == 1 ? "ON" : "OFF"); delay(200); ESP.restart(); break;
    case 'r': fakeExtraFps = v < 0 ? 0 : v; textf("fake filler %lu frames/s\n", (unsigned long)fakeExtraFps); break;
    case 'c': toggleRecording(); break;
    case 'd': dumpRecording(); break;
    case 'x': recCount = 0; textf("recording cleared\n"); break;
    default: textf("? (h = help)\n"); break;
  }
}

// =====================================================================================
//  Capture markers: one extended Rx-style frame into the GVRET stream (not on the truck).
//  Works during auto-detect because the button is polled in loop(), independent of rxTask.
// =====================================================================================
static void emitMarker() {
  if (markerNum < 255) markerNum++;
  else markerNum = 1;
  CFrame f;
  memset(&f, 0, sizeof(f));
  f.t_us = (uint32_t)esp_timer_get_time();   // same clock as received CAN frames
  f.id   = MARKER_CAN_ID | SNIFF_ID_EXT;
  f.dlc  = 8;
  uint32_t ms = millis();
  f.data[0] = 0xAA;
  f.data[1] = markerNum;
  f.data[2] = (uint8_t)(ms);
  f.data[3] = (uint8_t)(ms >> 8);
  f.data[4] = (uint8_t)(ms >> 16);
  f.data[5] = (uint8_t)(ms >> 24);
  f.data[6] = 0x55;
  f.data[7] = 0x55;
  // Queue only — do not count as a bus Rx frame. txTask emits it on GVRET if SavvyCAN is connected.
  if (xQueueSend(frameQ, &f, 0) != pdTRUE) { st_qDrops++; lastDropMs = millis(); }
  textf("MARK %u\n", markerNum);            // skipped on ports already in GVRET binary mode
  ledMarkFlashes = 3;
}

// Debounced press, active-low, ignore holds (one marker per click). 50 ms low to fire, 50 ms high to arm.
static void pollMarkerBtn(MarkerBtn& b) {
  if (b.pin < 0) return;
  bool released = digitalRead((uint8_t)b.pin) != LOW;
  uint32_t now = millis();
  if (released != b.level) {
    b.level = released;
    b.edgeMs = now;
  }
  uint32_t dt = now - b.edgeMs;
  if (!released && b.armed && dt >= MARKER_DEBOUNCE_MS) {
    b.armed = false;
    emitMarker();
  } else if (released && !b.armed && dt >= MARKER_DEBOUNCE_MS) {
    b.armed = true;
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
#if PIN_MARK >= 0
  pinMode(PIN_MARK, INPUT_PULLUP);
#endif

  prefs.begin("sniff", false);
  nodeId = prefs.getUChar("node", DEFAULT_NODE_ID);
  if (nodeId >= SNIFF_MAX_NODES) nodeId = 0;
  baudSetting = prefs.getULong("baud", DEFAULT_BAUD);
  fakeMode = prefs.getBool("fake", FAKE_BUS);
  pollEnabled = prefs.getBool("poll", POLL_DEFAULT_ON);

  gvUsb.begin(&Serial, nodeId + 1);
  gvUart.begin(&Serial0, nodeId + 1);

  frameQ = xQueueCreate(FRAME_QUEUE_LEN, sizeof(CFrame));
  pollQ  = xQueueCreate(16, sizeof(CFrame));
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

  xTaskCreatePinnedToCore(ledTask, "led", 2048, nullptr, 1, nullptr, CORE(0));
  xTaskCreatePinnedToCore(txTask, "tx", 6144, nullptr, 5, nullptr, CORE(1));
  xTaskCreatePinnedToCore(fakeTask, "fake", 4096, nullptr, 6, nullptr, CORE(1));
  xTaskCreatePinnedToCore(rxTask, "rx", 6144, nullptr, 10, nullptr, CORE(1));
  xTaskCreatePinnedToCore(pollTask, "poll", 4096, nullptr, 7, nullptr, CORE(1));
}

void loop() {
  static MarkerBtn bootBtn = {PIN_BUTTON, true, true, 0};
#if PIN_MARK >= 0
  static MarkerBtn markBtn = {PIN_MARK, true, true, 0};
  pollMarkerBtn(markBtn);
#endif
  pollMarkerBtn(bootBtn);
  delay(10);
}
