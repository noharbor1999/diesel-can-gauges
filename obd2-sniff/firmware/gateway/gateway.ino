// =====================================================================================
//  OBD2 SNIFF-EVERYTHING  -  GATEWAY  (ESP32-S3 on the laptop USB)
//  Receives ESP-NOW packets from up to 5 sniffer nodes and sends every frame to SavvyCAN
//  using the GVRET protocol. Node N -> GVRET bus N. Bus 5 = gateway status frames.
//
//  Nothing here transmits on any CAN bus (the gateway has no CAN transceiver at all, and
//  SavvyCAN "send frame" requests are discarded).
//
//  PORTS: plug the board's native "USB" port into the laptop (fast, recommended).
//    The "COM"/"UART" port also works (1,000,000 baud, about 5,000 frames/s max).
//    Whichever port SavvyCAN opens switches to GVRET binary. The other port (or the same
//    port before SavvyCAN connects) prints a plain-text status table every 2 s.
//    Text commands: h help, s status now, v print every frame as text, z zero counters.
//
//  LED (onboard RGB, WHITE only) blink count every 2.5 s:
//    1 = no nodes heard   2 = nodes heard, SavvyCAN not connected
//    3 = streaming to SavvyCAN, no drops   4 = drops in the last 5 s
// =====================================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <esp_timer.h>
#include "sniff_proto.h"
#include "gvret_port.h"

#define PIN_LED            48
#define LED_IS_RGB         1
#define UART_BAUD          1000000
#define RXQ_DEPTH          48        // packets buffered between Wi-Fi task and output
#define GW_STATUS_FRAMES   1         // 1 = send one status frame per node per second on bus 5
#define STATUS_ID_BASE     0x1F5C0000UL   // extended id + node number

struct RxItem { uint16_t len; uint8_t mac[6]; uint32_t rx_us; uint8_t data[SNIFF_PAYLOAD_MAX]; };

struct NodeInfo {
  bool     seen = false;
  uint8_t  mac[6];
  uint32_t lastSeenMs = 0;
  uint16_t nextSeq = 0;      bool seqInit = false;
  uint32_t nextIdx = 0;      bool idxInit = false;
  uint32_t frames = 0, lostFrames = 0, lostPkts = 0;
  uint32_t framesWin = 0, fps = 0;
  int64_t  offset = 0;       bool offInit = false;    // gateway_us - node_us
  int64_t  winMinOffset = INT64_MAX; uint32_t winStartMs = 0;
  uint32_t lastLossMs = 0;
  bool     fake = false;
  PktStats st = {};          bool hasStats = false;
};

static QueueHandle_t rxQ;
static NodeInfo nodes[SNIFF_MAX_NODES];
static GvretPort gvUsb, gvUart;
static String lineUsb, lineUart;
static bool verbose = false;
static volatile uint32_t rxQDrops = 0;
static uint32_t gwLossMs = 0;
static uint8_t ledCount = 1;
static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ---------- output ----------
static void textOut(const char* s) {
  if (!gvUsb.binary)  Serial.print(s);
  if (!gvUart.binary) Serial0.print(s);
}
static void textf(const char* fmt, ...) {
  char b[256]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof(b), fmt, ap); va_end(ap); textOut(b);
}
static inline void gvFrame(uint32_t ts, uint32_t id, bool ext, uint8_t bus, uint8_t dlc, const uint8_t* d) {
  if (gvUsb.binary)  gvUsb.sendFrame(ts, id, ext, bus, dlc, d);
  if (gvUart.binary) gvUart.sendFrame(ts, id, ext, bus, dlc, d);
}

// ---------- LED ----------
static void ledSet(bool on) {
#if LED_IS_RGB
  rgbLedWrite(PIN_LED, on ? 40 : 0, on ? 40 : 0, on ? 40 : 0);
#else
  digitalWrite(PIN_LED, on);
#endif
}
static void ledTask(void*) {
  for (;;) {
    uint8_t n = ledCount;
    for (uint8_t i = 0; i < n; i++) { ledSet(true); vTaskDelay(150); ledSet(false); vTaskDelay(250); }
    vTaskDelay(1500);
  }
}

// ---------- ESP-NOW receive (Wi-Fi task context: copy and get out) ----------
static void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len < (int)sizeof(PktHdr) || len > SNIFF_PAYLOAD_MAX) return;
  if (data[0] != SNIFF_MAGIC) return;
  static RxItem it;   // Wi-Fi task is single threaded
  it.len = len; memcpy(it.mac, info->src_addr, 6); it.rx_us = (uint32_t)esp_timer_get_time();
  memcpy(it.data, data, len);
  if (xQueueSend(rxQ, &it, 0) != pdTRUE) { rxQDrops++; gwLossMs = millis(); }
}

static void sendBeacon() {
  PktBeacon b = {SNIFF_MAGIC, SNIFF_VERSION, PKT_BEACON, SNIFF_WIFI_CHANNEL, millis()};
  esp_now_send(BCAST, (uint8_t*)&b, sizeof(b));
}

// ---------- packet processing ----------
static void processPacket(const RxItem& it) {
  const PktHdr* h = (const PktHdr*)it.data;
  if (h->version != SNIFF_VERSION || h->node >= SNIFF_MAX_NODES) return;
  NodeInfo& n = nodes[h->node];
  uint32_t now = millis();
  if (n.seen && memcmp(n.mac, it.mac, 6) != 0) {
    textf("WARNING: two boards are using node id %u (%02X:%02X:%02X:%02X:%02X:%02X)\n", h->node,
          it.mac[0], it.mac[1], it.mac[2], it.mac[3], it.mac[4], it.mac[5]);
  }
  n.seen = true; memcpy(n.mac, it.mac, 6); n.lastSeenMs = now; n.fake = h->flags & 1;

  // packet sequence gaps (frames + stats packets share one counter)
  if (n.seqInit && h->seq != n.nextSeq) {
    uint16_t gap = (uint16_t)(h->seq - n.nextSeq);
    if (gap < 1000) { n.lostPkts += gap; n.lastLossMs = now; }
  }
  n.nextSeq = h->seq + 1; n.seqInit = true;

  if (h->type == PKT_STATS && it.len >= (int)sizeof(PktStats)) {
    memcpy(&n.st, it.data, sizeof(PktStats)); n.hasStats = true;
    return;
  }
  if (h->type != PKT_FRAMES) return;

  // exact lost-frame count from the running frame index
  if (n.idxInit && h->first_idx != n.nextIdx) {
    uint32_t gap = h->first_idx - n.nextIdx;
    if (gap < 1000000) { n.lostFrames += gap; n.lastLossMs = now; }
  }
  n.nextIdx = h->first_idx + h->count; n.idxInit = true;

  // map node clock to gateway clock: keep the smallest (rx - node) seen in a 5 s window
  int64_t cand = (int64_t)it.rx_us - (int64_t)h->t_base_us;
  if (!n.offInit) { n.offset = cand; n.offInit = true; n.winStartMs = now; }
  if (cand < n.winMinOffset) n.winMinOffset = cand;
  if (cand < n.offset) n.offset = cand;
  if (now - n.winStartMs > 5000) { n.offset = n.winMinOffset; n.winMinOffset = INT64_MAX; n.winStartMs = now; }

  const uint8_t* p = it.data + sizeof(PktHdr);
  const uint8_t* end = it.data + it.len;
  for (uint8_t i = 0; i < h->count; i++) {
    if (p + SNIFF_REC_HDR > end) break;
    uint16_t dt = p[0] | (p[1] << 8);
    uint32_t id; memcpy(&id, p + 2, 4);
    uint8_t dlc = p[6]; if (dlc > 8) dlc = 8;
    if (p + SNIFF_REC_HDR + dlc > end) break;
    const uint8_t* d = p + SNIFF_REC_HDR;
    uint32_t ts = (uint32_t)((int64_t)h->t_base_us + dt + n.offset);
    bool ext = id & SNIFF_ID_EXT;
    gvFrame(ts, id & 0x1FFFFFFF, ext, h->node, dlc, d);
    if (verbose) {
      char b[100]; int k = snprintf(b, sizeof(b), "n%u %10lu %s%08lX [%u]", h->node, (unsigned long)ts, ext ? "x" : " ", (unsigned long)(id & 0x1FFFFFFF), dlc);
      for (int j = 0; j < dlc; j++) k += snprintf(b + k, sizeof(b) - k, " %02X", d[j]);
      snprintf(b + k, sizeof(b) - k, "\n"); textOut(b);
    }
    n.frames++; n.framesWin++;
    p += SNIFF_REC_HDR + dlc;
  }
}

// ---------- status ----------
static const char* stateName(uint8_t s) {
  switch (s) { case 1: return "SEARCH"; case 2: return "NO-GW"; case 3: return "OK"; case 4: return "DROPS";
               case 5: return "BUS-ERR"; case 6: return "FAKE"; default: return "?"; }
}
static void printStatus() {
  uint32_t now = millis();
  textf("\n--- gateway  ch %d  up %lus  queue drops %lu  SavvyCAN: %s ---\n", SNIFF_WIFI_CHANNEL, now / 1000,
        (unsigned long)rxQDrops, (gvUsb.binary || gvUart.binary) ? (gvUsb.binary ? "USB port" : "COM port") : "not connected");
  textf("node bus speed   state    fps   frames      lost(%%)          pkt_lost bus_err nodeQdrop seen\n");
  for (int i = 0; i < SNIFF_MAX_NODES; i++) {
    NodeInfo& n = nodes[i];
    if (!n.seen) continue;
    double pct = (n.frames + n.lostFrames) ? 100.0 * n.lostFrames / (n.frames + n.lostFrames) : 0;
    textf("%3d  %2d %6lu  %-7s %5lu %9lu %8lu(%5.2f) %9lu %7lu %9lu %4.1fs%s\n", i, i,
          (unsigned long)(n.hasStats ? n.st.baud : 0), n.hasStats ? stateName(n.st.state) : "?",
          (unsigned long)n.fps, (unsigned long)n.frames, (unsigned long)n.lostFrames, pct, (unsigned long)n.lostPkts,
          (unsigned long)(n.hasStats ? n.st.bus_errors : 0), (unsigned long)(n.hasStats ? n.st.q_drops + n.st.rx_missed + n.st.rx_overrun : 0),
          (now - n.lastSeenMs) / 1000.0, n.fake ? "  (fake bus)" : "");
  }
}

static void sendStatusFrames() {
#if GW_STATUS_FRAMES
  uint32_t ts = (uint32_t)esp_timer_get_time();
  for (int i = 0; i < SNIFF_MAX_NODES; i++) {
    NodeInfo& n = nodes[i];
    if (!n.seen) continue;
    uint8_t d[8];
    uint16_t fps = min<uint32_t>(n.fps, 65535);
    uint16_t lost = min<uint32_t>(n.lostFrames, 65535);
    d[0] = fps; d[1] = fps >> 8; d[2] = lost; d[3] = lost >> 8;
    d[4] = n.hasStats ? n.st.state : 0;
    d[5] = (millis() - n.lastSeenMs) / 100 > 255 ? 255 : (millis() - n.lastSeenMs) / 100;
    uint32_t b = n.hasStats ? n.st.baud : 0;
    uint16_t kbps = b / 1000; d[6] = kbps >> 8; d[7] = kbps;   // bus speed in kbit/s, big-endian (500 = 01 F4)
    gvFrame(ts, STATUS_ID_BASE | i, true, SNIFF_STATUS_BUS, 8, d);
  }
#endif
}

static void handleLine(String& line) {
  line.trim(); if (!line.length()) return;
  switch (line[0]) {
    case 'h': case '?':
      textf("gateway: h help | s status | v toggle frame text | z zero counters\n"); break;
    case 's': printStatus(); break;
    case 'v': verbose = !verbose; break;
    case 'z': for (auto& n : nodes) { n.frames = n.lostFrames = n.lostPkts = 0; } rxQDrops = 0; textf("counters zeroed\n"); break;
    default: textf("? (h = help)\n");
  }
}

static void pollPort(Stream& s, GvretPort& gv, String& line) {
  while (s.available()) {
    int c = s.read(); if (c < 0) break;
    if (gv.binary || c == 0xE7 || c == 0xF1) { gv.feed((uint8_t)c); continue; }
    if (c == '\n' || c == '\r') { if (line.length()) { handleLine(line); line = ""; } }
    else if (line.length() < 40) line += (char)c;
  }
}

void setup() {
  Serial.begin(115200); Serial.setTxTimeoutMs(0);
  Serial0.begin(UART_BAUD);
#if !LED_IS_RGB
  pinMode(PIN_LED, OUTPUT);
#endif
  rxQ = xQueueCreate(RXQ_DEPTH, sizeof(RxItem));
  gvUsb.begin(&Serial, SNIFF_MAX_NODES + 1);   // buses 0..4 = nodes, 5 = status
  gvUart.begin(&Serial0, SNIFF_MAX_NODES + 1);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_channel(SNIFF_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) Serial.println("ESP-NOW init FAILED");
  esp_now_register_recv_cb(onRecv);
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, BCAST, 6); p.channel = SNIFF_WIFI_CHANNEL; p.ifidx = WIFI_IF_STA;
  esp_now_add_peer(&p);

  xTaskCreatePinnedToCore(ledTask, "led", 2048, nullptr, 1, nullptr, 0);
  delay(300);
  textf("\nOBD2 sniff gateway  MAC %s  channel %d  (h = help)\n", WiFi.macAddress().c_str(), SNIFF_WIFI_CHANNEL);
}

void loop() {
  static uint32_t lastBeacon = 0, lastSec = 0, lastPrint = 0;
  pollPort(Serial, gvUsb, lineUsb);
  pollPort(Serial0, gvUart, lineUart);

  static RxItem it;
  int n = 0;
  while (n < 16 && xQueueReceive(rxQ, &it, n == 0 ? pdMS_TO_TICKS(2) : 0) == pdTRUE) { processPacket(it); n++; }

  uint32_t now = millis();
  if (now - lastBeacon >= 500) { lastBeacon = now; sendBeacon(); }
  if (now - lastSec >= 1000) {
    uint32_t el = now - lastSec; lastSec = now;
    for (auto& nd : nodes) { nd.fps = nd.framesWin * 1000 / (el ? el : 1); nd.framesWin = 0; }
    sendStatusFrames();
  }
  if (now - lastPrint >= 2000) { lastPrint = now; if (!(gvUsb.binary && gvUart.binary)) printStatus(); }

  // LED state
  bool any = false, loss = (gwLossMs && now - gwLossMs < 5000);
  for (auto& nd : nodes) { if (nd.seen && now - nd.lastSeenMs < 3000) any = true; if (nd.lastLossMs && now - nd.lastLossMs < 5000) loss = true; }
  bool sav = gvUsb.binary || gvUart.binary;
  ledCount = !any ? 1 : (!sav ? 2 : (loss ? 4 : 3));
}
