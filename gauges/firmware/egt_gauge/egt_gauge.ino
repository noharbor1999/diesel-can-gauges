// =====================================================================================
// Truck EGT gauge v7 - the author, Nov 15, 2026
// ESP32-C3 SuperMini + Adafruit MAX31856 (K) + Waveshare 1.28" GC9A01 round LCD + EC11 encoder w/ push
// Probe: pre-turbo, cylinder 6 manifold.
//
// v7 adds: cool-down cue, buzzer alarm, backlight PWM + auto-dim (light sensor), drive log in flash,
//          Bluetooth LE (live + history + log; see egt_phone.html), scrolling settings list, LOG screen.
//
// CONTROLS (no menu)
//   TURN (browsing)  = change screen: MAIN > HISTORY > LOG > SETTINGS > MAIN (wraps)
//   MAIN     CLICK = reset peak AND silence the current DANGER alarm
//   HISTORY  CLICK = enter; TURN = 5/10/15/30 MIN or BACK; CLICK = done
//   LOG      CLICK = enter; TURN = THIS DRIVE / older drives / BACK; CLICK = done
//   SETTINGS CLICK = enter; TURN = scroll list; CLICK = edit; TURN = change; CLICK = save
//            CLEAR LOG asks NO / YES first. BACK leaves the list.
//   30 s with no input -> MAIN (an open edit is cancelled, not saved). DANGER -> MAIN at once.
//
// PINS (see README v7 wiring table)
//   SCK 4, MISO 5, MOSI 6 (shared)   LCD CS 7, DC 10, RST -> 3V3 (software reset; frees GPIO1)
//   MAX31856 CS 0                     ENC A 20, B 21, SW 3
//   LIGHT SENSOR (LDR divider) GPIO1 = ADC1_CH1 (not a strapping pin)
//   LCD BL PWM GPIO8   (strapping pin, but "any value" in SPI boot; see README)
//   BUZZER GPIO2, ACTIVE LOW through a PNP+NPN driver whose base resistors pull GPIO2 HIGH at reset
//   GPIO9 left free (BOOT button).
// =====================================================================================

#ifndef HOST_RENDER
#include <SPI.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <Adafruit_MAX31856.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "driver/gpio.h"
#else
#include "host_shim.h"
#endif

// ======================= DEFAULTS (deg F) - "(knob)" = editable in SETTINGS, saved =======================
const int DEF_HOT       = 1200;  // (knob) background yellow from here
const int DEF_HIGH      = 1400;  // (knob) background red from here; HIGH events are logged
const int DEF_DANGER    = 1600;  // (knob) blinking red + alarm from here
const int DEF_PK_HOLD_S = 30;    // (knob) peak holds this long after the last new high, then drops to live (0 = OFF)
const int DEF_COOL_F    = 300;   // (knob) COOL DOWN target. EDUCATED GUESS (not from a source) - tune on the truck
const int DEF_BUZZER    = 1;     // (knob) 1 = alarm sounds
const int DEF_DAY_BRT   = 100;   // (knob) backlight % in daylight
const int DEF_NIGHT_BRT = 30;    // (knob) backlight % when dark
const int DEF_AUTO_DIM  = 1;     // (knob) 1 = light sensor picks DAY/NIGHT; 0 = always DAY
const int DEF_DIM_AT    = 20;    // (knob) light % below which it counts as dark
const int STEP_F        = 25;    // knob step for temperatures

// ---- behaviour ----
const uint16_t BLINK_MS          = 250;
const uint32_t IDLE_MS           = 30000;  // no input this long -> MAIN
const int      ENC_STEPS_PER_DETENT = 4;   // set 2 if one detent moves two items
const int      DETENTS_PER_SCREEN   = 1;   // detents needed to flip screens
const int      BAND_HYST_F       = 25;     // colour/alarm steps DOWN only this far below a threshold (stops flicker)
const int      COOL_REARM_F      = 100;    // after OK TO SHUT OFF, cue clears when EGT is this far above COOL AT
const bool     COOL_CUE_WHILE_HOT = false; // false: COOL DOWN shows once EGT is back under HOT (colour says the rest)
const bool     HIGH_CHIRP        = true;   // short chirp when EGT enters HIGH (only if BUZZER ON)
const uint32_t CHIRP_MIN_GAP_MS  = 10000;  // at most one HIGH chirp per 10 s
const uint16_t CHIRP_MS          = 60;
const uint16_t DANGER_PATTERN[]  = {150, 100, 150, 700};  // ms: on, off, on, off - repeats during a DANGER event
const bool     BUZZ_ACTIVE_LOW   = true;   // driver in README: GPIO LOW = buzzer on

// ---- backlight / light sensor ----
const uint32_t BL_PWM_HZ        = 2000;
const uint8_t  BL_PWM_BITS      = 8;
const bool     LDR_BRIGHT_IS_LOW = true;   // true: 10k 3V3->pin, LDR pin->GND (bright = low voltage). Flip if reversed.
const int      DIM_HYST_PCT     = 5;       // light must go this far past DIM AT to switch back to DAY
const uint32_t DIM_DWELL_MS     = 4000;    // light must stay past the threshold this long (ignores passing lights)

// ---- drive log (NVS flash) ----
const int      LOG_N           = 10;       // drives kept
const int      LOG_PEAKS       = 3;        // top HIGH events kept per drive
const uint32_t LOG_SAVE_MS     = 180000;   // current drive saved at most every 3 min, plus at the end of each HIGH event
const uint32_t LOG_MIN_RUN_S   = 60;       // power-ups shorter than this are not logged

// ---- Bluetooth LE ----
const bool     BLE_ENABLE      = true;
const char    *BLE_NAME        = "EGT Gauge";
const uint32_t BLE_NOTIFY_MS   = 500;
#define SVC_UUID  "d71c5700-b298-4fbf-911d-f4ed8b480e08"
#define LIVE_UUID "d71c5701-b298-4fbf-911d-f4ed8b480e08"   // read/notify: 16-byte live packet
#define CTRL_UUID "d71c5702-b298-4fbf-911d-f4ed8b480e08"   // write: [cmd][u16 offset]  cmd 1 = history, 2 = log
#define DATA_UUID "d71c5703-b298-4fbf-911d-f4ed8b480e08"   // read: answer to the last CTRL write
// ========================================================================================================

#define PIN_SCK   4
#define PIN_MISO  5
#define PIN_MOSI  6
#define TFT_CS    7
#define TFT_DC    10
#define TFT_RST   -1   // LCD RST tied to 3V3; library sends a software reset
#define TC_CS     0
#define ENC_A     20
#define ENC_B     21
#define ENC_SW    3
#define PIN_LDR   1
#define PIN_BL    8
#define PIN_BUZZ  2

#define C_GREEN  0x07E0
#define C_YELLOW 0xFFE0
#define C_RED    0xF800
#define C_BLACK  0x0000
#define C_WHITE  0xFFFF

// ---------------------------------- types (all before any function) ----------------------------------
enum Screen { S_MAIN, S_HIST, S_LOG, S_SET, N_SCREENS };
enum SetId { ST_HOT, ST_HIGH, ST_DANGER, ST_PKHOLD, ST_COOL, ST_BUZZ, ST_DAYBRT, ST_NIGHTBRT, ST_AUTODIM, ST_DIMAT,
             N_VAL, ST_CLEARLOG = N_VAL, ST_BACK, N_SET };
enum Kind { K_TEMP, K_SECS, K_PCT, K_ONOFF, K_ACTION };
struct SetDef { const char *name; const char *key; int def; int step; Kind kind; };
enum Cool { CD_NONE, CD_NEED, CD_OK };          // cool-down state
enum Cue { CUE_NONE, CUE_COOL, CUE_OK, CUE_MUTED };
struct DriveRec {                               // 22 bytes, all uint16 (no padding); same layout over BLE
  uint16_t num, maxF, runMin, secHigh, secDanger;
  uint16_t pkF[LOG_PEAKS], pkSec[LOG_PEAKS];    // top HIGH events: peak F and seconds above HIGH, biggest first
};

const SetDef SET[N_SET] = {
  {"HOT",       "hot",      DEF_HOT,       STEP_F, K_TEMP},
  {"HIGH",      "high",     DEF_HIGH,      STEP_F, K_TEMP},
  {"DANGER",    "danger",   DEF_DANGER,    STEP_F, K_TEMP},
  {"PK HOLD",   "pkhold",   DEF_PK_HOLD_S, 10,     K_SECS},
  {"COOL AT",   "cool",     DEF_COOL_F,    STEP_F, K_TEMP},
  {"BUZZER",    "buzz",     DEF_BUZZER,    1,      K_ONOFF},
  {"DAY BRT",   "daybrt",   DEF_DAY_BRT,   5,      K_PCT},
  {"NIGHT BRT", "nightbrt", DEF_NIGHT_BRT, 5,      K_PCT},
  {"AUTO DIM",  "autodim",  DEF_AUTO_DIM,  1,      K_ONOFF},
  {"DIM AT",    "dimat",    DEF_DIM_AT,    5,      K_PCT},
  {"CLEAR LOG", nullptr,    0,             0,      K_ACTION},
  {"BACK",      nullptr,    0,             0,      K_ACTION},
};

// ---------------------------------- globals ----------------------------------
#ifndef HOST_RENDER
Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_RST);
Adafruit_MAX31856 tc(TC_CS);
#else
GFXcanvas16 tft(240, 240);
#endif
Preferences prefs;

int val[N_VAL];                                 // live settings
#define HOT_F    val[ST_HOT]
#define HIGH_F   val[ST_HIGH]
#define DANGER_F val[ST_DANGER]

// history: 1 sample/s, 30 min, RAM only (cleared at key-off)
const int HIST_N = 1800;
int16_t hist[HIST_N];
int histHead = 0, histCount = 0;
uint32_t histSeq = 0;                           // number of samples taken since boot (BLE alignment)
const int WIN_MIN[] = {5, 10, 15, 30};
int winIdx = 0;

// measurement state
float egtF = 0, peakF = 0;
bool haveEgt = false, fault = true;
uint8_t faultBits = 0;
uint32_t peakSetAt = 0;
int band = 0;                                   // 0 OK, 1 HOT, 2 HIGH, 3 DANGER (with hysteresis)
bool alarmOn = false, muted = false;
uint32_t alarmStart = 0;
bool chirpOn = false; uint32_t chirpStart = 0, lastChirp = 0; bool chirpEver = false;
Cool cool = CD_NONE;
bool blinkOn = true;

// drive log
DriveRec logv[LOG_N]; int logCount = 0;
DriveRec cur;                                   // this drive (maxF, peaks, num); counters below
uint32_t runSec = 0, secHigh = 0, secDanger = 0;
bool inEvent = false; float evPeak = 0; uint32_t evSec = 0;
bool curSaved = false, logSaveReq = false; uint32_t lastLogSave = 0;
uint16_t driveNum = 1;

// backlight
float lightPct = 100; bool dark = false; uint32_t darkSince = 0; int blShown = -1;

// UI
Screen scr = S_MAIN;
bool active = false, editing = false, confirm = false;
int histSel = 0, setSel = 0, logSel = 0, editVal = 0, confirmSel = 0, turnAcc = 0;
bool dirty = true;
uint32_t lastInput = 0;

// BLE
bool bleConn = false, bleWasConn = false;

// ---------------------------------- prototypes ----------------------------------
void go(int s);
void startChirp(uint32_t now);
DriveRec snapshot();

// ---------------------------------- small helpers ----------------------------------
uint16_t u16c(uint32_t v) { return v > 65535 ? 65535 : (uint16_t)v; }
int bandThr(int b) { return b == 3 ? DANGER_F : b == 2 ? HIGH_F : HOT_F; }

// ---------------------------------- encoder ----------------------------------
volatile int encRaw = 0;
volatile uint8_t encState = 0;
void IRAM_ATTR encISR() {                       // runs on CHANGE of A or B; full quadrature table
  static const int8_t T[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
  encState = ((encState << 2) | (digitalRead(ENC_A) << 1) | digitalRead(ENC_B)) & 0x0F;
  encRaw += T[encState];
}
int readTurns() {
  static int used = 0;
  noInterrupts(); int r = encRaw; interrupts();
  int d = (r - used) / ENC_STEPS_PER_DETENT;
  used += d * ENC_STEPS_PER_DETENT;
  return d;
}
bool readClick() {
  static bool last = HIGH; static uint32_t t = 0;
  bool s = digitalRead(ENC_SW);
  if (s != last && millis() - t > 30) { t = millis(); last = s; if (s == LOW) return true; }
  return false;
}

// ---------------------------------- settings ----------------------------------
void limits(int i, int &lo, int &hi) {
  switch (i) {
    case ST_HOT:      lo = max(200, val[ST_COOL] + STEP_F); hi = HIGH_F - STEP_F; break;
    case ST_HIGH:     lo = HOT_F + STEP_F;  hi = DANGER_F - STEP_F; break;
    case ST_DANGER:   lo = HIGH_F + STEP_F; hi = 2400; break;
    case ST_PKHOLD:   lo = 0;   hi = 600; break;
    case ST_COOL:     lo = 100; hi = min(800, HOT_F - STEP_F); break;
    case ST_DAYBRT:
    case ST_NIGHTBRT: lo = 5;   hi = 100; break;
    case ST_DIMAT:    lo = 5;   hi = 95; break;
    default:          lo = 0;   hi = 1; break;   // ON/OFF
  }
}
void loadSettings() {
  for (int i = 0; i < N_VAL; i++) val[i] = prefs.getInt(SET[i].key, SET[i].def);
  bool okT = val[ST_COOL] >= 100 && val[ST_COOL] < HOT_F && HOT_F < HIGH_F && HIGH_F < DANGER_F && DANGER_F <= 2400;
  if (!okT) {                                   // bad/old combo -> temperature defaults
    HOT_F = DEF_HOT; HIGH_F = DEF_HIGH; DANGER_F = DEF_DANGER; val[ST_COOL] = DEF_COOL_F;
    for (int i : {ST_HOT, ST_HIGH, ST_DANGER, ST_COOL}) prefs.putInt(SET[i].key, val[i]);
  }
  for (int i = 0; i < N_VAL; i++) { int lo, hi; limits(i, lo, hi); val[i] = constrain(val[i], lo, hi); }
}
void saveSetting(int i) { if (i < N_VAL && SET[i].key) prefs.putInt(SET[i].key, val[i]); }  // one key per save
void fmtVal(char *b, int n, int i, int v) {
  switch (SET[i].kind) {
    case K_SECS:  if (v == 0) snprintf(b, n, "OFF"); else snprintf(b, n, "%ds", v); break;
    case K_PCT:   snprintf(b, n, "%d%%", v); break;
    case K_ONOFF: snprintf(b, n, "%s", v ? "ON" : "OFF"); break;
    default:      snprintf(b, n, "%d", v); break;
  }
}

// ---------------------------------- drive log ----------------------------------
void insertPeak(DriveRec &r, float pk, uint32_t sec) {
  uint16_t p = u16c((uint32_t)max(0.0f, pk + 0.5f)), s = u16c(sec);
  if (p <= r.pkF[LOG_PEAKS - 1]) return;
  int k = LOG_PEAKS - 1;
  while (k > 0 && r.pkF[k - 1] < p) { r.pkF[k] = r.pkF[k - 1]; r.pkSec[k] = r.pkSec[k - 1]; k--; }
  r.pkF[k] = p; r.pkSec[k] = s;
}
DriveRec snapshot() {                           // this drive as it would be stored now
  DriveRec r = cur;
  r.num = driveNum; r.runMin = u16c(runSec / 60); r.secHigh = u16c(secHigh); r.secDanger = u16c(secDanger);
  if (inEvent) insertPeak(r, evPeak, evSec);
  return r;
}
void loadLog() {
  logCount = constrain((int)prefs.getUChar("logn", 0), 0, LOG_N);
  if (logCount && prefs.getBytes("log", logv, sizeof(DriveRec) * logCount) != sizeof(DriveRec) * logCount) logCount = 0;
}
void saveLog() { prefs.putBytes("log", logv, sizeof(DriveRec) * logCount); prefs.putUChar("logn", (uint8_t)logCount); }
void pushLog(const DriveRec &r) {
  memmove(&logv[1], &logv[0], sizeof(DriveRec) * (LOG_N - 1));
  logv[0] = r; if (logCount < LOG_N) logCount++;
}
void bootLog() {                                // roll the last power cycle's drive into the list (1 write per boot)
  loadLog();
  if (prefs.isKey("cur")) {
    DriveRec r;
    if (prefs.getBytes("cur", &r, sizeof r) == sizeof r) { pushLog(r); saveLog(); }
    prefs.remove("cur");
  }
  driveNum = prefs.getUShort("drv", 0) + 1;
  memset(&cur, 0, sizeof cur); cur.num = driveNum;
}
void saveCur(uint32_t now) {
  DriveRec r = snapshot();
  prefs.putBytes("cur", &r, sizeof r);
  if (!curSaved) { prefs.putUShort("drv", driveNum); curSaved = true; }
  lastLogSave = now; logSaveReq = false;
}
void clearLog() {
  logCount = 0; prefs.remove("log"); prefs.putUChar("logn", 0); prefs.remove("cur");
  memset(&cur, 0, sizeof cur); cur.num = driveNum;
  runSec = secHigh = secDanger = 0; inEvent = false; curSaved = false; logSaveReq = false;
}
void endEvent() { insertPeak(cur, evPeak, evSec); inEvent = false; logSaveReq = true; }

// ---------------------------------- measurement logic ----------------------------------
// Called every 200 ms with the amp reading (deg C) and fault flag.
void processSample(float c, bool flt, uint32_t now) {
  bool wasFault = fault; fault = flt;
  if (fault != wasFault) dirty = true;
  if (fault) {                                  // no probe: no alarm, close any open event
    haveEgt = false; band = 0; alarmOn = muted = false;
    if (inEvent) endEvent();
    return;
  }
  float nf = c * 9.0f / 5.0f + 32.0f;
  egtF = haveEgt ? egtF * 0.5f + nf * 0.5f : nf; haveEgt = true;

  // peak with PK HOLD (holds N s after the last new high, then drops to live)
  if (egtF > peakF) { peakF = egtF; peakSetAt = now; }
  else if (val[ST_PKHOLD] > 0 && now - peakSetAt >= (uint32_t)val[ST_PKHOLD] * 1000UL) { peakF = egtF; peakSetAt = now; }

  // band with hysteresis
  int raw = egtF >= DANGER_F ? 3 : egtF >= HIGH_F ? 2 : egtF >= HOT_F ? 1 : 0;
  int old = band;
  if (raw > band) band = raw;
  while (band > raw && egtF < bandThr(band) - BAND_HYST_F) band--;
  if (band == 2 && old < 2) startChirp(now);     // entering HIGH (not straight to DANGER)
  if (band == 3 && !alarmOn) { alarmOn = true; muted = false; alarmStart = now; }
  if (band < 3 && alarmOn) { alarmOn = false; muted = false; }

  // HIGH events for the drive log
  if (egtF > cur.maxF) cur.maxF = u16c((uint32_t)(egtF + 0.5f));
  if (egtF >= HIGH_F) { if (!inEvent) { inEvent = true; evPeak = 0; evSec = 0; } if (egtF > evPeak) evPeak = egtF; }
  else if (inEvent && egtF < HIGH_F - BAND_HYST_F) endEvent();

  // cool-down cue
  if (egtF >= HOT_F) cool = CD_NEED;
  else if (cool == CD_NEED && egtF <= val[ST_COOL]) cool = CD_OK;
  else if (cool == CD_OK && egtF > val[ST_COOL] + COOL_REARM_F) cool = CD_NONE;
}
// Called once per second.
void tick1s(uint32_t now) {
  hist[histHead] = (fault || !haveEgt) ? 0 : (int16_t)egtF;
  histHead = (histHead + 1) % HIST_N;
  if (histCount < HIST_N) histCount++;
  histSeq++;
  runSec++;
  if (!fault && haveEgt) {
    if (egtF >= HIGH_F) secHigh++;
    if (egtF >= DANGER_F) secDanger++;
    if (inEvent) evSec++;
  }
  if (runSec >= LOG_MIN_RUN_S && (logSaveReq || !curSaved || now - lastLogSave >= LOG_SAVE_MS)) saveCur(now);
}
Cue cueNow() {
  if (fault) return CUE_NONE;
  if (alarmOn) return muted ? CUE_MUTED : CUE_NONE;
  if (cool == CD_NEED) return (band == 0 || COOL_CUE_WHILE_HOT) ? CUE_COOL : CUE_NONE;
  if (cool == CD_OK) return CUE_OK;
  return CUE_NONE;
}

// ---------------------------------- buzzer ----------------------------------
void setBuzz(bool on) {
  static int shown = -1;
  if ((int)on == shown) return; shown = on;
  digitalWrite(PIN_BUZZ, (on ^ BUZZ_ACTIVE_LOW) ? HIGH : LOW);
}
void startChirp(uint32_t now) {
  if (!HIGH_CHIRP || !val[ST_BUZZ]) return;
  if (chirpEver && now - lastChirp < CHIRP_MIN_GAP_MS) return;
  chirpOn = true; chirpEver = true; chirpStart = lastChirp = now;
}
void buzzerTick(uint32_t now) {
  bool want = false;
  if (val[ST_BUZZ] && alarmOn && !muted) {
    uint32_t total = 0; for (uint16_t d : DANGER_PATTERN) total += d;
    uint32_t ph = (now - alarmStart) % total, acc = 0; int k = 0;
    for (uint16_t d : DANGER_PATTERN) { if (ph < acc + d) break; acc += d; k++; }
    want = (k % 2) == 0;                        // even entries = on
  } else if (chirpOn) {
    if (now - chirpStart < CHIRP_MS) want = true; else chirpOn = false;
  }
  setBuzz(want);
}

// ---------------------------------- backlight ----------------------------------
int brightTarget() {
  if (scr == S_SET && editing && (setSel == ST_DAYBRT || setSel == ST_NIGHTBRT)) return editVal;  // live preview
  return (val[ST_AUTODIM] && dark) ? val[ST_NIGHTBRT] : val[ST_DAYBRT];
}
void lightTick(uint32_t now, int adcRaw) {        // every 200 ms
  float pct = adcRaw * 100.0f / 4095.0f;
  if (LDR_BRIGHT_IS_LOW) pct = 100.0f - pct;
  lightPct = lightPct * 0.8f + pct * 0.2f;
  bool wantDark = dark ? !(lightPct > val[ST_DIMAT] + DIM_HYST_PCT) : (lightPct < val[ST_DIMAT]);
  if (wantDark == dark) darkSince = now;
  else if (now - darkSince >= DIM_DWELL_MS) { dark = wantDark; darkSince = now; }
}
void applyBacklight() {
  int b = brightTarget();
  if (b == blShown) return; blShown = b;
#ifndef HOST_RENDER
  ledcWrite(PIN_BL, (uint32_t)b * ((1 << BL_PWM_BITS) - 1) / 100);
#endif
}

// ---------------------------------- drawing helpers ----------------------------------
void txtAt(const char *s, int cx, int y, uint8_t size, uint16_t fg, uint16_t bg) {   // centred on cx
  int16_t x1, y1; uint16_t w, h;
  tft.setTextSize(size);
  tft.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  tft.setTextColor(fg, bg);
  tft.setCursor(cx - (int)w / 2, y);
  tft.print(s);
}
void txt(const char *s, int y, uint8_t size, uint16_t fg, uint16_t bg) { txtAt(s, 120, y, size, fg, bg); }

uint16_t bandBg() {
  if (fault) return C_BLACK;
  if (band == 3) return blinkOn ? C_RED : C_BLACK;
  if (band == 2) return C_RED;
  if (band == 1) return C_YELLOW;
  return C_GREEN;
}
uint16_t fgOn(uint16_t bg) { return (bg == C_GREEN || bg == C_YELLOW) ? C_BLACK : C_WHITE; }

void pageDots() {
  for (int i = 0; i < N_SCREENS; i++) {
    int x = 120 + (2 * i - (N_SCREENS - 1)) * 7;
    if (i == scr) tft.fillCircle(x, 226, 4, C_WHITE); else tft.drawCircle(x, 226, 4, C_WHITE);
  }
}
void hint(const char *s, int y = 202) { txt(s, y, 1, C_WHITE, C_BLACK); }
void fmtMS(char *b, int n, uint32_t s) { snprintf(b, n, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60)); }
void arrow(int y, bool up) {                    // small scroll arrow centred on x = 120
  if (up) tft.fillTriangle(114, y + 5, 126, y + 5, 120, y, C_WHITE);
  else    tft.fillTriangle(114, y, 126, y, 120, y + 5, C_WHITE);
}

// ---------------------------------- MAIN: 24-segment ring ----------------------------------
// 24 segments x 100 F (0..2400) on a 270 deg arc, open at the bottom. Lit = live EGT.
// Peak segment stays lit + wedge outside the ring. Centre number = PEAK. Cue box in the bottom gap.
const int GAUGE_MAX = 2400, SEG_F = 100, NSEG = GAUGE_MAX / SEG_F;
const int CX = 120, CY = 120, R_IN = 94, R_OUT = 112;
const int CUE_X = 70, CUE_Y = 186, CUE_W = 100, CUE_H = 36;   // inside the bottom gap of the ring
float angOf(float f) { f = constrain(f, 0.0f, (float)GAUGE_MAX); return 225.0f - 270.0f * f / GAUGE_MAX; }
void polar(float deg, float r, int &x, int &y) { float a = deg * DEG_TO_RAD; x = CX + r * cos(a); y = CY - r * sin(a); }

void drawSeg(int k, bool lit, uint16_t fg, uint16_t bg) {
  float a0 = angOf(k * SEG_F) - 1.5f, a1 = angOf((k + 1) * SEG_F) + 1.5f;   // 3 deg gap between segments
  int ax, ay, bx, by, cx, cy, dx, dy;
  for (float a = a0; a > a1; a -= 2.0f) {
    float n = max(a - 2.0f, a1);
    polar(a, R_IN, ax, ay); polar(a, R_OUT, bx, by); polar(n, R_IN, cx, cy); polar(n, R_OUT, dx, dy);
    uint16_t c = lit ? fg : bg;
    tft.fillTriangle(ax, ay, bx, by, cx, cy, c); tft.fillTriangle(bx, by, dx, dy, cx, cy, c);
  }
  {                                             // outline always in fg: unlit = outline only; lit = fill + same
    polar(a0, R_IN, ax, ay); polar(a0, R_OUT, bx, by); tft.drawLine(ax, ay, bx, by, fg);
    polar(a1, R_IN, ax, ay); polar(a1, R_OUT, bx, by); tft.drawLine(ax, ay, bx, by, fg);
    for (float a = a0; a > a1; a -= 2.0f) {
      float n = max(a - 2.0f, a1);
      polar(a, R_IN, ax, ay); polar(n, R_IN, cx, cy); tft.drawLine(ax, ay, cx, cy, fg);
      polar(a, R_OUT, bx, by); polar(n, R_OUT, dx, dy); tft.drawLine(bx, by, dx, dy, fg);
    }
  }
}
void drawLabels(uint16_t fg, uint16_t bg) {     // speedometer labels, x100 F
  for (int f = 0; f <= GAUGE_MAX; f += 400) {
    char b[4]; snprintf(b, 4, "%d", f / 100); int x, y; polar(angOf(f), 80, x, y);
    tft.setTextSize(2); tft.setTextColor(fg, bg);
    tft.setCursor(x - (f >= 1000 ? 12 : 6), y - 7); tft.print(b);
  }
  txt("x100 F", 172, 1, fg, bg);
}
void drawPeakWedge(float f, uint16_t c) {
  float a = angOf(f); int x0, y0, x1, y1, x2, y2;
  polar(a, R_OUT + 1, x0, y0); polar(a - 4, 120, x1, y1); polar(a + 4, 120, x2, y2);
  tft.fillTriangle(x0, y0, x1, y1, x2, y2, c);
}
void drawPeakNum(int pk, uint16_t fg, uint16_t bg) {
  char b[8]; snprintf(b, 8, "%d", pk);
  tft.fillRect(54, 92, 132, 44, bg); txt(b, 96, 5, fg, bg);
  txt("PEAK F", 142, 2, fg, bg);
}
void drawCue(Cue q, uint16_t fg, uint16_t bg) {
  tft.fillRect(CUE_X - 2, CUE_Y - 2, CUE_W + 4, CUE_H + 4, bg);
  if (q == CUE_NONE) return;
  tft.fillRoundRect(CUE_X, CUE_Y, CUE_W, CUE_H, 6, fg);
  if (q == CUE_COOL)  { txt("COOL", CUE_Y + 3, 2, bg, fg); txt("DOWN", CUE_Y + 19, 2, bg, fg); }
  if (q == CUE_OK)    { txt("OK TO", CUE_Y + 3, 2, bg, fg); txt("SHUT OFF", CUE_Y + 19, 2, bg, fg); }
  if (q == CUE_MUTED) { txt("MUTED", CUE_Y + 11, 2, bg, fg); }
}
int segsFor(float f) { return constrain((int)(f / SEG_F + 0.999f), 0, NSEG); }  // any heat in a segment lights it

void drawMain() {
  static uint16_t curBg = 0x1234; static int shownN = -1, shownPkSeg = -1, shownPk = -1, shownCue = -1;
  uint16_t bg = bandBg(), fg = fgOn(bg);
  if (dirty || bg != curBg) {
    tft.fillScreen(bg); curBg = bg; dirty = false; shownN = shownPkSeg = shownPk = shownCue = -1;
    if (fault) { txt("NO", 70, 5, fg, bg); txt("PROBE", 122, 4, fg, bg); txt("CHECK PYRO PLUG", 170, 1, fg, bg); return; }
    drawLabels(fg, bg);
  }
  if (fault) return;
  int n = segsFor(egtF), pkSeg = max(segsFor(peakF) - 1, 0), pk = (int)(peakF + 0.5f);
  if (n != shownN || pkSeg != shownPkSeg) {
    for (int k = 0; k < NSEG; k++) {
      bool lit = (k < n) || (k == pkSeg && pk > 0);
      bool was = (k < shownN) || (k == shownPkSeg && shownPk > 0);
      if (shownN < 0 || lit != was) drawSeg(k, lit, fg, bg);
    }
    shownN = n; shownPkSeg = pkSeg;
  }
  if (pk != shownPk) {                          // wedge follows the exact peak, not just its segment
    if (shownPk >= 0) drawPeakWedge(shownPk, bg);
    drawPeakWedge(pk, fg);
    drawPeakNum(pk, fg, bg); shownPk = pk;
  }
  Cue q = cueNow();
  if ((int)q != shownCue) { drawCue(q, fg, bg); shownCue = q; }
}

// ---------------------------------- HISTORY ----------------------------------
const char *HOPT[] = {"5", "10", "15", "30", "BACK"};
void drawHist() {
  static uint32_t lastDraw = 0;
  bool full = dirty;
  if (!full && millis() - lastDraw < 1000) return;
  dirty = false; lastDraw = millis();
  const int X0 = 30, Y0 = 52, W = 180, H = 90;
  char b[24];
  if (full) {                                   // static parts only on a full redraw (no 1 Hz flicker)
    tft.fillScreen(C_BLACK);
    tft.drawRect(X0 - 1, Y0 - 1, W + 2, H + 2, C_WHITE);
    snprintf(b, 24, "LAST %d MIN", WIN_MIN[winIdx]); txt(b, 30, 2, C_WHITE, C_BLACK);
    if (active) {
      for (int i = 0; i < 4; i++) {             // 5 10 15 30 on one row, BACK under it
        int x = 39 + i * 42; bool sel = (i == histSel);
        tft.fillRect(x, 172, 36, 22, sel ? C_WHITE : C_BLACK);
        if (!sel) tft.drawRect(x, 172, 36, 22, C_WHITE);
        txtAt(HOPT[i], x + 18, 176, 2, sel ? C_BLACK : C_WHITE, sel ? C_WHITE : C_BLACK);
      }
      bool sel = (histSel == 4);
      tft.fillRect(88, 198, 64, 22, sel ? C_WHITE : C_BLACK);
      if (!sel) tft.drawRect(88, 198, 64, 22, C_WHITE);
      txtAt("BACK", 120, 202, 2, sel ? C_BLACK : C_WHITE, sel ? C_WHITE : C_BLACK);
    } else { hint("CLICK TO CHANGE", 180); pageDots(); }
  }
  int win = WIN_MIN[winIdx] * 60, n = min(win, histCount);
  int lo = 0, hi = max(DANGER_F + 200, 400), pk = 0;
  for (int i = 0; i < n; i++) { int v = hist[(histHead - 1 - i + HIST_N) % HIST_N]; if (v > pk) pk = v; }
  if (pk > hi) hi = pk + 100;
  auto yOf = [&](int v) { return (int)(Y0 + H - (long)(v - lo) * H / (hi - lo)); };
  tft.fillRect(X0, Y0, W, H, C_BLACK);
  tft.drawFastHLine(X0, yOf(HOT_F), W, C_YELLOW);
  tft.drawFastHLine(X0, yOf(HIGH_F), W, C_RED);
  tft.drawFastHLine(X0, yOf(DANGER_F), W, C_RED);
  int prevY = -1;
  for (int x = 0; x < W && n > 0; x++) {
    int age = win - 1 - (int)((long)x * win / W);
    if (age >= n) continue;
    int v = hist[(histHead - 1 - age + HIST_N) % HIST_N];
    int y = constrain(yOf(v), Y0, Y0 + H - 1);
    if (prevY >= 0) tft.drawLine(X0 + x - 1, prevY, X0 + x, y, C_WHITE); else tft.drawPixel(X0 + x, y, C_WHITE);
    prevY = y;
  }
  tft.fillRect(14, 150, 212, 16, C_BLACK);
  snprintf(b, 24, "PK %d NOW %d", pk, fault ? 0 : (int)egtF); txt(b, 150, 2, C_WHITE, C_BLACK);
}

// ---------------------------------- LOG ----------------------------------
int logEntries() { return 1 + logCount; }        // 0 = this drive, then stored drives newest first
void drawLogEntry(const DriveRec &r, bool live) {
  char b[24];
  if (live) snprintf(b, 24, "THIS DRIVE"); else snprintf(b, 24, "DRIVE %u", (unsigned)r.num);
  txt(b, 22, 2, C_WHITE, C_BLACK);
  snprintf(b, 24, "MAX %uF", (unsigned)r.maxF); txt(b, 44, 3, C_WHITE, C_BLACK);
  snprintf(b, 24, "RUN %u MIN", (unsigned)r.runMin); txt(b, 76, 2, C_WHITE, C_BLACK);
  char t[12];
  fmtMS(t, 12, r.secHigh);   snprintf(b, 24, "HIGH %s", t);   txt(b, 98, 2, C_WHITE, C_BLACK);
  fmtMS(t, 12, r.secDanger); snprintf(b, 24, "DANGER %s", t); txt(b, 120, 2, C_WHITE, C_BLACK);
  txt("TOP PEAKS: F / TIME ABOVE HIGH", 144, 1, C_WHITE, C_BLACK);
  if (r.pkF[0] == 0) { txt("NONE", 160, 2, C_WHITE, C_BLACK); return; }
  for (int k = 0; k < LOG_PEAKS; k++) {          // three columns, 64 px apart
    if (r.pkF[k] == 0) continue;
    int cx = 120 + (k - 1) * 64;
    snprintf(b, 24, "%u", (unsigned)r.pkF[k]); txtAt(b, cx, 156, 2, C_WHITE, C_BLACK);
    fmtMS(t, 12, r.pkSec[k]); txtAt(t, cx, 176, 2, C_WHITE, C_BLACK);
  }
}
void drawLog() {
  static DriveRec shown; static uint32_t lastChk = 0;
  bool liveShown = !active || logSel == 0;
  DriveRec r = liveShown ? snapshot() : (logSel <= logCount ? logv[logSel - 1] : snapshot());
  if (!dirty) {                                  // redraw only when the shown numbers change (max 1/s)
    if (!liveShown || millis() - lastChk < 1000) return;
    lastChk = millis();
    if (memcmp(&r, &shown, sizeof r) == 0) return;
  }
  dirty = false; shown = r;
  tft.fillScreen(C_BLACK);
  if (active && logSel == logEntries()) {        // BACK entry
    txt("DRIVE LOG", 70, 2, C_WHITE, C_BLACK);
    tft.fillRect(60, 104, 120, 34, C_WHITE); txt("BACK", 108, 3, C_BLACK, C_WHITE);
    hint("CLICK = EXIT", 160);
    return;
  }
  drawLogEntry(r, liveShown);
  char b[28];
  if (active) {
    snprintf(b, 28, "%d / %d", logSel + 1, logEntries() + 1); hint(b, 200);
  } else {
    if (logCount == 0) hint("NO SAVED DRIVES YET", 200); else hint("CLICK TO BROWSE", 200);
    pageDots();
  }
}

// ---------------------------------- SETTINGS ----------------------------------
void rowText(char *b, int n, int i) {
  if (SET[i].kind == K_ACTION) { snprintf(b, n, "%s", SET[i].name); return; }
  char v[12]; fmtVal(v, 12, i, val[i]); snprintf(b, n, "%s %s", SET[i].name, v);
}
void drawSet() {
  static uint32_t lastLive = 0;
  bool liveEdit = editing && setSel == ST_DIMAT;  // shows the live light reading
  char b[28];
  if (!dirty) {                                  // only the LIGHT NOW line updates, twice a second
    if (!liveEdit || millis() - lastLive < 500) return;
    lastLive = millis();
    tft.fillRect(20, 140, 200, 16, C_BLACK);
    snprintf(b, 28, "LIGHT NOW %d%%", (int)(lightPct + 0.5f)); txt(b, 140, 2, C_WHITE, C_BLACK);
    return;
  }
  dirty = false; lastLive = millis();
  tft.fillScreen(C_BLACK);
  if (confirm) {
    txt("CLEAR LOG?", 58, 3, C_WHITE, C_BLACK);
    txt("ERASES ALL DRIVES", 96, 2, C_WHITE, C_BLACK);
    const char *o[2] = {"NO", "YES"};
    for (int i = 0; i < 2; i++) {
      int x = 46 + i * 84; bool sel = (i == confirmSel);
      tft.fillRect(x, 130, 64, 32, sel ? C_WHITE : C_BLACK);
      if (!sel) tft.drawRect(x, 130, 64, 32, C_WHITE);
      txtAt(o[i], x + 32, 135, 3, sel ? C_BLACK : C_WHITE, sel ? C_WHITE : C_BLACK);
    }
    hint("TURN = PICK   CLICK = OK", 180);
    return;
  }
  if (editing) {
    txt(SET[setSel].name, 50, 3, C_WHITE, C_BLACK);
    fmtVal(b, 28, setSel, editVal); txt(b, 86, 5, C_WHITE, C_BLACK);
    const char *h;
    switch (SET[setSel].kind) {
      case K_SECS:  h = "TURN +/-10s   CLICK = SAVE"; break;
      case K_PCT:   h = "TURN +/-5%   CLICK = SAVE"; break;
      case K_ONOFF: h = "TURN = ON/OFF   CLICK = SAVE"; break;
      default:      h = "TURN +/-25 F   CLICK = SAVE"; break;
    }
    if (setSel == ST_DIMAT) { snprintf(b, 28, "LIGHT NOW %d%%", (int)(lightPct + 0.5f)); txt(b, 140, 2, C_WHITE, C_BLACK); }
    if (setSel == ST_DAYBRT || setSel == ST_NIGHTBRT) txt("SCREEN SHOWS IT NOW", 140, 1, C_WHITE, C_BLACK);
    hint(h, 170);
    hint("NO INPUT 30 s = CANCEL", 184);
    return;
  }
  if (!active) {                                 // browsing: first rows, no highlight
    txt("SETTINGS", 26, 2, C_WHITE, C_BLACK);
    for (int i = 0; i < 5; i++) { rowText(b, 28, i); txt(b, 52 + i * 26, 2, C_WHITE, C_BLACK); }
    arrow(184, false);
    hint("CLICK TO EDIT", 196); pageDots();
    return;
  }
  txt("SETTINGS", 16, 2, C_WHITE, C_BLACK);      // scrolling list: selected row in the middle
  for (int d = -2; d <= 2; d++) {
    int i = setSel + d; if (i < 0 || i >= N_SET) continue;
    int y = 120 + d * 30;                        // row centre
    rowText(b, 28, i);
    if (d == 0) { tft.fillRect(20, y - 13, 200, 26, C_WHITE); txt(b, y - 7, 2, C_BLACK, C_WHITE); }
    else txt(b, y - 7, 2, C_WHITE, C_BLACK);
  }
  if (setSel - 2 > 0) arrow(38, true);
  if (setSel + 2 < N_SET - 1) arrow(197, false);
  snprintf(b, 28, "%d / %d", setSel + 1, N_SET); hint(b, 208);
}

void go(int s) { scr = (Screen)s; active = editing = confirm = false; turnAcc = 0; dirty = true; }

// ---------------------------------- input ----------------------------------
void handleInput(int t, bool c, uint32_t now) {
  if (t || c) lastInput = now;
  if (!active) {                                 // browsing: turn flips screens
    if (t) {
      turnAcc += t;
      if (abs(turnAcc) >= DETENTS_PER_SCREEN) {
        int step = turnAcc > 0 ? 1 : -1; turnAcc = 0;
        go((scr + step + N_SCREENS) % N_SCREENS);
      }
    }
    if (c) {
      if (scr == S_MAIN) { peakF = egtF; peakSetAt = now; if (alarmOn) muted = true; dirty = true; }  // reset peak + silence
      else if (scr == S_HIST) { active = true; histSel = winIdx; dirty = true; }
      else if (scr == S_LOG) { active = true; logSel = 0; dirty = true; }
      else if (scr == S_SET) { active = true; setSel = 0; dirty = true; }
    }
  } else if (scr == S_HIST) {
    if (t) { histSel = constrain(histSel + t, 0, 4); if (histSel < 4) winIdx = histSel; dirty = true; }
    if (c) { active = false; dirty = true; }
  } else if (scr == S_LOG) {
    if (t) { logSel = constrain(logSel + t, 0, logEntries()); dirty = true; }
    if (c) { active = false; dirty = true; }
  } else if (scr == S_SET && confirm) {
    if (t) { confirmSel = constrain(confirmSel + t, 0, 1); dirty = true; }
    if (c) { if (confirmSel == 1) clearLog(); confirm = false; dirty = true; }
  } else if (scr == S_SET && !editing) {
    if (t) { setSel = constrain(setSel + t, 0, N_SET - 1); dirty = true; }
    if (c) {
      if (setSel < N_VAL) { editVal = val[setSel]; editing = true; }
      else if (setSel == ST_CLEARLOG) { confirm = true; confirmSel = 0; }
      else active = false;                       // BACK
      dirty = true;
    }
  } else if (scr == S_SET && editing) {
    if (t) {
      int lo, hi; limits(setSel, lo, hi);
      if (SET[setSel].kind == K_ONOFF) { if (t % 2) editVal = !editVal; }
      else editVal = constrain(editVal + t * SET[setSel].step, lo, hi);
      if (setSel == ST_BUZZ && editVal) { chirpOn = true; chirpStart = now; }   // test beep
      dirty = true;
    }
    if (c) { val[setSel] = editVal; saveSetting(setSel); editing = false; dirty = true; }
  }
  if ((scr != S_MAIN || active) && now - lastInput > IDLE_MS) go(S_MAIN);
}

// ---------------------------------- Bluetooth LE ----------------------------------
void putU16(uint8_t *p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
void putI16(uint8_t *p, int16_t v) { putU16(p, (uint16_t)v); }
int buildLive(uint8_t *p) {                      // 16 bytes, little-endian; flags mirror what MAIN shows
  p[0] = 1;                                      // packet version
  p[1] = fault ? 255 : (uint8_t)band;
  p[2] = (alarmOn ? 1 : 0) | (muted ? 2 : 0) | (cueNow() == CUE_COOL ? 4 : 0) | (cueNow() == CUE_OK ? 8 : 0) |
         (fault ? 16 : 0) | (dark ? 32 : 0) | (val[ST_BUZZ] ? 64 : 0);
  p[3] = (uint8_t)constrain((int)(lightPct + 0.5f), 0, 100);
  putI16(p + 4, fault ? 0 : (int16_t)egtF); putI16(p + 6, (int16_t)(peakF + 0.5f));
  putU16(p + 8, HOT_F); putU16(p + 10, HIGH_F); putU16(p + 12, DANGER_F); putU16(p + 14, val[ST_COOL]);
  return 16;
}
// cmd 1: history chunk. [1][n][u16 total][u32 seqNewest][u16 ageOffset][n x i16 newest-first]
// cmd 2: log.           [2][count][u16 driveNum][count x 22-byte DriveRec], entry 0 = this drive
const int BLE_HIST_CHUNK = 240;
uint8_t bleBuf[10 + 2 * BLE_HIST_CHUNK];
int buildData(uint8_t cmd, uint16_t off) {
  // Runs in the BLE task. Reads RAM the loop also writes; a read may be one sample out of step (harmless).
  if (cmd == 1) {
    int total = histCount, head = histHead; uint32_t seq = histSeq;
    int n = constrain(total - (int)off, 0, BLE_HIST_CHUNK);
    bleBuf[0] = 1; bleBuf[1] = (uint8_t)n; putU16(bleBuf + 2, total);
    bleBuf[4] = seq & 0xFF; bleBuf[5] = (seq >> 8) & 0xFF; bleBuf[6] = (seq >> 16) & 0xFF; bleBuf[7] = seq >> 24;
    putU16(bleBuf + 8, off);
    for (int i = 0; i < n; i++) putI16(bleBuf + 10 + 2 * i, hist[(head - 1 - (off + i) + 2 * HIST_N) % HIST_N]);
    return 10 + 2 * n;
  }
  int cnt = 1 + logCount;
  bleBuf[0] = 2; bleBuf[1] = (uint8_t)cnt; putU16(bleBuf + 2, driveNum);
  DriveRec r = snapshot();
  for (int k = 0; k < cnt; k++) {
    const DriveRec &d = (k == 0) ? r : logv[k - 1];
    const uint16_t *w = (const uint16_t *)&d;
    for (int j = 0; j < (int)(sizeof(DriveRec) / 2); j++) putU16(bleBuf + 4 + k * sizeof(DriveRec) + 2 * j, w[j]);
  }
  return 4 + cnt * sizeof(DriveRec);
}

#ifndef HOST_RENDER
BLECharacteristic *chLive = nullptr, *chData = nullptr;
class SrvCB : public BLEServerCallbacks {
  void onConnect(BLEServer *) override { bleConn = true; }
  void onDisconnect(BLEServer *) override { bleConn = false; }
};
class CtrlCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    String v = c->getValue();
    if (v.length() < 1) return;
    uint8_t cmd = (uint8_t)v[0];
    uint16_t off = v.length() >= 3 ? (uint8_t)v[1] | ((uint8_t)v[2] << 8) : 0;
    if (cmd != 1 && cmd != 2) return;
    int n = buildData(cmd, off);
    chData->setValue(bleBuf, n);
  }
};
void bleBegin() {
  BLEDevice::init(BLE_NAME);
  BLEServer *srv = BLEDevice::createServer();
  srv->setCallbacks(new SrvCB());
  BLEService *svc = srv->createService(SVC_UUID);
  chLive = svc->createCharacteristic(LIVE_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
#if defined(CONFIG_BLUEDROID_ENABLED)
  chLive->addDescriptor(new BLE2902());
#endif
  BLECharacteristic *chCtrl = svc->createCharacteristic(CTRL_UUID, BLECharacteristic::PROPERTY_WRITE);
  chCtrl->setCallbacks(new CtrlCB());
  chData = svc->createCharacteristic(DATA_UUID, BLECharacteristic::PROPERTY_READ);
  svc->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SVC_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
}
void bleTick(uint32_t now) {
  static uint32_t lastNotify = 0, discAt = 0;
  if (bleWasConn && !bleConn) discAt = now;                      // just dropped
  if (!bleConn && discAt && now - discAt > 500) { BLEDevice::startAdvertising(); discAt = 0; }
  bleWasConn = bleConn;
  if (now - lastNotify < BLE_NOTIFY_MS) return;
  lastNotify = now;
  uint8_t p[16]; int n = buildLive(p);
  chLive->setValue(p, n);
  if (bleConn) chLive->notify();
}

// ---------------------------------- setup / loop ----------------------------------
void setup() {
  gpio_set_level((gpio_num_t)PIN_BUZZ, BUZZ_ACTIVE_LOW ? 1 : 0);  // latch "off" before the pin becomes an output
  pinMode(PIN_BUZZ, OUTPUT); digitalWrite(PIN_BUZZ, BUZZ_ACTIVE_LOW ? HIGH : LOW);
  Serial.begin(115200);
  pinMode(ENC_A, INPUT_PULLUP); pinMode(ENC_B, INPUT_PULLUP); pinMode(ENC_SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_A), encISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), encISR, CHANGE);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_LDR, ADC_11db);

  prefs.begin("egt", false);
  loadSettings();
  bootLog();

  ledcAttach(PIN_BL, BL_PWM_HZ, BL_PWM_BITS);
  applyBacklight();

  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI);
  tft.begin(); tft.setRotation(0); tft.fillScreen(C_BLACK);
  txt("EGT", 100, 4, C_WHITE, C_BLACK);

  if (!tc.begin()) Serial.println("MAX31856 not found");
  tc.setThermocoupleType(MAX31856_TCTYPE_K);
  tc.setNoiseFilter(MAX31856_NOISE_FILTER_60HZ);
  tc.setConversionMode(MAX31856_CONTINUOUS);
  if (BLE_ENABLE) bleBegin();
  delay(300);
  lastInput = millis();
}

void loop() {
  static uint32_t lastRead = 0, lastHist = 0, lastBlink = 0;
  uint32_t now = millis();
  if (now - lastRead >= 200) {
    lastRead = now;
    faultBits = tc.readFault();
    float c = tc.readThermocoupleTemperature();
    processSample(c, (faultBits & (MAX31856_FAULT_OPEN | MAX31856_FAULT_OVUV)) || isnan(c), now);
    lightTick(now, analogRead(PIN_LDR));
  }
  if (now - lastHist >= 1000) { lastHist = now; tick1s(now); }
  if (now - lastBlink >= BLINK_MS) { lastBlink = now; blinkOn = !blinkOn; }

  handleInput(readTurns(), readClick(), now);
  if (band == 3 && scr != S_MAIN) go(S_MAIN);   // DANGER overrides every screen
  buzzerTick(now);
  applyBacklight();
  if (BLE_ENABLE) bleTick(now);

  switch (scr) {
    case S_MAIN: drawMain(); break;
    case S_HIST: drawHist(); break;
    case S_LOG:  drawLog();  break;
    case S_SET:  drawSet();  break;
    default: break;
  }
}
#endif  // HOST_RENDER
