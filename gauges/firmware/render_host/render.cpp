// Host build of the REAL egt_gauge.ino drawing + logic code against Adafruit_GFX/GFXcanvas16.
//   ./render out      -> writes out/<name>.raw (RGB565, 240x240) for every screen/state
//   ./render --test   -> runs logic tests (state machine, PK HOLD, alarm/mute, cool-down, log, NVS writes, rollover)
#define HOST_RENDER
#include <cstdint>
uint32_t g_ms = 1000;
int g_pin[64];
#include "../egt_gauge/egt_gauge.ino"
#include <string>
#include <vector>

static std::string outDir = "out";
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); fails++; } else printf("ok:   %s\n", msg); } while (0)

void dump(const char *name) {
  std::string p = outDir + "/" + name + ".raw";
  FILE *f = fopen(p.c_str(), "wb"); fwrite(tft.getBuffer(), 2, 240 * 240, f); fclose(f);
}
void defaults() {
  for (int i = 0; i < N_VAL; i++) val[i] = SET[i].def;
  fault = false; haveEgt = true; band = 0; alarmOn = muted = false; cool = CD_NONE; blinkOn = true;
  scr = S_MAIN; active = editing = confirm = false; dirty = true; lightPct = 63;
}
void setEgt(float e, float pk) {
  egtF = e; peakF = pk;
  band = e >= DANGER_F ? 3 : e >= HIGH_F ? 2 : e >= HOT_F ? 1 : 0; alarmOn = band == 3;
}
void draw() {
  dirty = true;
  switch (scr) { case S_MAIN: drawMain(); break; case S_HIST: drawHist(); break; case S_LOG: drawLog(); break; case S_SET: drawSet(); break; default: break; }
}
void fillHistory() {               // 30 min synthetic drive: cruise, a tow climb, a hard pull, cooling
  histHead = 0; histCount = 0;
  for (int i = 0; i < HIST_N; i++) {
    double t = i, v = 820 + 60 * sin(t / 37.0) + 25 * sin(t / 5.3);
    if (i > 500 && i < 640) v += 600 * sin((i - 500) / 140.0 * M_PI);          // tow climb ~1450
    if (i > 1420 && i < 1480) v += 820 * sin((i - 1420) / 60.0 * M_PI);        // hard pull ~1650
    if (i > 1650) v = 820 - (i - 1650) * 2.5;                                  // coasting / idle
    hist[histHead] = (int16_t)v; histHead = (histHead + 1) % HIST_N; histCount++;
  }
}
void fillLog() {
  logCount = 0;
  DriveRec r; memset(&r, 0, sizeof r);
  for (int k = 0; k < 8; k++) {
    memset(&r, 0, sizeof r); r.num = 14 + k; r.maxF = 1100 + 60 * k; r.runMin = 20 + 7 * k;
    if (r.maxF >= 1400) { r.secHigh = 30 * k; r.pkF[0] = r.maxF; r.pkSec[0] = 25 * k; r.pkF[1] = 1420; r.pkSec[1] = 12; }
    if (k == 7) { r.secDanger = 12; r.pkF[2] = 1405; r.pkSec[2] = 4; }
    pushLog(r);
  }
  memset(&cur, 0, sizeof cur); driveNum = 22; cur.num = 22; cur.maxF = 1652;
  cur.pkF[0] = 1652; cur.pkSec[0] = 41; cur.pkF[1] = 1458; cur.pkSec[1] = 95; cur.pkF[2] = 1412; cur.pkSec[2] = 8;
  runSec = 47 * 60 + 12; secHigh = 144; secDanger = 12; inEvent = false;
}

int renders() {
  defaults(); fillHistory(); fillLog();
  setEgt(950, 1100);  draw(); dump("01_main_green");
  setEgt(1290, 1380); draw(); dump("02_main_yellow");
  setEgt(1480, 1520); draw(); dump("03_main_red");
  setEgt(1650, 1700); blinkOn = true; draw(); dump("04_main_danger_blink_on");
  blinkOn = false; draw(); dump("05_main_danger_blink_off");
  blinkOn = true; muted = true; draw(); dump("06_main_danger_muted");
  muted = false;
  setEgt(620, 640); cool = CD_NEED; draw(); dump("07_main_cool_down");
  setEgt(285, 300); cool = CD_OK; draw(); dump("08_main_ok_to_shut_off");
  cool = CD_NONE; fault = true; band = 0; alarmOn = false; draw(); dump("09_main_no_probe");
  fault = false; setEgt(780, 810);
  scr = S_HIST; winIdx = 3; active = false; draw(); dump("10_history_browse_30min");
  active = true; histSel = winIdx = 1; draw(); dump("11_history_active_10min");
  histSel = 4; winIdx = 1; draw(); dump("12_history_active_back");
  active = false; winIdx = 0; draw(); dump("13_history_browse_5min");
  scr = S_LOG; active = false; draw(); dump("14_log_browse_this_drive");
  active = true; logSel = 1; draw(); dump("15_log_active_drive21");
  logSel = 8; draw(); dump("16_log_drive14_no_events");
  logSel = logEntries(); draw(); dump("17_log_active_back");
  scr = S_SET; active = false; draw(); dump("18_settings_browse");
  active = true; setSel = 0; draw(); dump("19_settings_list_top");
  setSel = 5; draw(); dump("20_settings_list_middle");
  setSel = ST_BACK; draw(); dump("21_settings_list_end");
  editing = true;
  setSel = ST_HOT; editVal = 1225; draw(); dump("22_edit_hot");
  setSel = ST_PKHOLD; editVal = 30; draw(); dump("23_edit_pk_hold");
  editVal = 0; draw(); dump("24_edit_pk_hold_off");
  setSel = ST_COOL; editVal = 300; draw(); dump("25_edit_cool_at");
  setSel = ST_BUZZ; editVal = 1; draw(); dump("26_edit_buzzer");
  setSel = ST_DAYBRT; editVal = 100; draw(); dump("27_edit_day_brt");
  setSel = ST_NIGHTBRT; editVal = 30; draw(); dump("28_edit_night_brt");
  setSel = ST_AUTODIM; editVal = 1; draw(); dump("29_edit_auto_dim");
  setSel = ST_DIMAT; editVal = 20; draw(); dump("30_edit_dim_at");
  editing = false; setSel = ST_CLEARLOG; confirm = true; confirmSel = 0; draw(); dump("31_clear_log_confirm_no");
  confirmSel = 1; draw(); dump("32_clear_log_confirm_yes");
  return 0;
}

// ---------------- logic tests ----------------
float FtoC(float f) { return (f - 32) * 5 / 9; }
uint32_t t0;
// run the same order as loop(): 200 ms samples, 1 s ticks, input, danger->MAIN, buzzer
void run(uint32_t ms, float f, int turns = 0, bool click = false) {
  static uint32_t lastRead = 0, lastHist = 0;
  uint32_t end = g_ms + ms; bool first = true;
  while ((int32_t)(end - g_ms) > 0) {
    g_ms += 10;
    if (g_ms - lastRead >= 200) { lastRead = g_ms; processSample(FtoC(f), false, g_ms); }
    if (g_ms - lastHist >= 1000) { lastHist = g_ms; tick1s(g_ms); }
    handleInput(first ? turns : 0, first ? click : false, g_ms); first = false;
    if (band == 3 && scr != S_MAIN) go(S_MAIN);
    buzzerTick(g_ms);
  }
}
int tests() {
  prefs.m.clear(); prefs.writes = 0;
  loadSettings(); bootLog(); fault = true; haveEgt = false; band = 0; cool = CD_NONE; scr = S_MAIN; lastInput = g_ms;
  CHECK(val[ST_HOT] == 1200 && val[ST_COOL] == 300 && val[ST_PKHOLD] == 30, "defaults loaded");
  // PK HOLD
  run(5000, 900); run(3000, 1100); CHECK(fabs(peakF - 1100) < 2, "peak follows rise");
  run(10000, 950); CHECK(peakF > 1090, "peak held 10 s after last high");
  run(21000, 950); CHECK(fabs(peakF - 950) < 2, "peak drops to live after PK HOLD 30 s");
  run(1000, 950, 0, true); CHECK(fabs(peakF - 950) < 2, "MAIN click resets peak");
  // bands, hysteresis, chirp, alarm, mute
  run(2000, 1410); CHECK(band == 2, "HIGH band at 1410");
  CHECK(chirpEver, "chirp fired on entering HIGH");
  run(2000, 1390); CHECK(band == 2, "stays red at 1390 (hysteresis)");
  run(2000, 1370); CHECK(band == 1, "drops to yellow below 1375");
  run(2000, 1620); CHECK(alarmOn && !muted, "DANGER alarm on");
  { uint32_t s = alarmStart; g_ms = s + 50; buzzerTick(g_ms); CHECK(g_pin[PIN_BUZZ] == LOW, "buzzer pin LOW (on) in first beep"); g_ms = s + 200; buzzerTick(g_ms); CHECK(g_pin[PIN_BUZZ] == HIGH, "buzzer pin HIGH (off) in gap"); }
  scr = S_SET; active = true; run(400, 1620); CHECK(scr == S_MAIN && !active, "DANGER forces MAIN");
  run(400, 1620, 0, true); CHECK(muted, "click on MAIN mutes alarm");
  { bool any = false; for (int i = 0; i < 200; i++) { run(10, 1620); if (g_pin[PIN_BUZZ] == LOW) any = true; } CHECK(!any, "muted: buzzer stays off"); }
  run(2000, 1590); CHECK(alarmOn && muted, "still same event at 1590 (hysteresis), still muted");
  run(2000, 1560); CHECK(!alarmOn && !muted, "event ends below 1575, mute cleared");
  run(2000, 1620); CHECK(alarmOn && !muted, "new DANGER event sounds again");
  // cool-down
  run(3000, 1250); CHECK(cool == CD_NEED && cueNow() == CUE_NONE, "hot: NEED, no cue while still >= HOT band");
  run(3000, 1100); CHECK(cueNow() == CUE_COOL, "under HOT: COOL DOWN");
  run(3000, 290); CHECK(cueNow() == CUE_OK, "under COOL AT: OK TO SHUT OFF");
  run(3000, 350); CHECK(cueNow() == CUE_OK, "small rise keeps OK");
  run(3000, 450); CHECK(cueNow() == CUE_NONE, "driving again clears cue");
  // knob carousel / settings
  go(S_MAIN); lastInput = g_ms;
  run(100, 800, 1); CHECK(scr == S_HIST, "turn 1 -> HISTORY");
  run(100, 800, 1); CHECK(scr == S_LOG, "turn 2 -> LOG");
  run(100, 800, 1); CHECK(scr == S_SET, "turn 3 -> SETTINGS");
  run(100, 800, 1); CHECK(scr == S_MAIN, "turn 4 wraps -> MAIN");
  run(100, 800, -1); CHECK(scr == S_SET, "turn -1 wraps -> SETTINGS");
  run(100, 800, 0, true); CHECK(active && setSel == 0, "click enters list");
  run(100, 800, 0, true); CHECK(editing && editVal == 1200, "click edits HOT");
  run(100, 800, 20); CHECK(editVal == 1375, "HOT capped at HIGH-25");
  run(100, 800, 0, true); CHECK(!editing && val[ST_HOT] == 1375 && prefs.getInt("hot", 0) == 1375, "HOT saved to NVS");
  run(100, 800, 0, true); run(100, 800, -7); run(100, 800, 0, true); CHECK(val[ST_HOT] == 1200, "HOT set back to 1200");
  run(100, 800, 4); CHECK(setSel == ST_COOL, "scroll to COOL AT");
  run(100, 800, 0, true); run(100, 800, 100); CHECK(editVal == 800, "COOL AT capped at 800");
  run(31000, 800); CHECK(scr == S_MAIN && !editing && val[ST_COOL] == 300, "30 s idle -> MAIN, edit cancelled");
  run(100, 800, -1); run(100, 800, 0, true); run(100, 800, 6); CHECK(setSel == ST_DAYBRT, "scroll to DAY BRT");
  run(100, 800, 0, true); run(100, 800, -4); CHECK(brightTarget() == 80, "brightness preview while editing");
  run(100, 800, 0, true); CHECK(val[ST_DAYBRT] == 80 && brightTarget() == 80, "DAY BRT saved");
  run(100, 800, 50); CHECK(setSel == ST_BACK, "list clamps at BACK");
  run(100, 800, 0, true); CHECK(!active && scr == S_SET, "BACK leaves list");
  // auto-dim
  val[ST_AUTODIM] = 1; dark = false; lightPct = 60;
  for (int i = 0; i < 10; i++) { g_ms += 200; lightTick(g_ms, 4000); } CHECK(!dark, "short darkness ignored (dwell)");
  for (int i = 0; i < 40; i++) { g_ms += 200; lightTick(g_ms, 4000); } CHECK(dark && brightTarget() == val[ST_NIGHTBRT], "dark -> NIGHT level");
  for (int i = 0; i < 40; i++) { g_ms += 200; lightTick(g_ms, 3400); } CHECK(dark, "light 17% stays NIGHT (hysteresis)");
  for (int i = 0; i < 60; i++) { g_ms += 200; lightTick(g_ms, 1000); } CHECK(!dark, "bright -> DAY");
  // drive log + flash writes over a 2 h drive with 3 HIGH events
  int w0 = prefs.writes;
  for (int m = 0; m < 120; m++) {
    if (m == 30 || m == 60 || m == 90) { run(40000, 1450 + m); run(20000, 900); }
    else run(60000, 850);
  }
  int w = prefs.writes - w0;
  printf("      NVS writes in 2 h: %d\n", w);
  CHECK(w < 60, "log writes stay rare (< 60 in 2 h)");
  DriveRec before = snapshot();
  CHECK(before.pkF[0] >= 1615, "top peak (1620 alarm test) recorded");
  printf("      peaks %u/%us %u/%us %u/%us  high %us\n", before.pkF[0], before.pkSec[0], before.pkF[1], before.pkSec[1], before.pkF[2], before.pkSec[2], before.secHigh);
  CHECK(before.pkF[1] >= 1535 && before.pkSec[1] >= 38 && before.pkF[2] >= 1505, "top 3 events kept, sorted, with duration");
  // simulate key-off/on: last saved "cur" rolls into the log
  int nBefore = logCount; uint16_t num = driveNum;
  bootLog();
  CHECK(logCount == nBefore + 1 && logv[0].num == num, "power cycle rolls drive into log");
  CHECK(logv[0].maxF >= 1535 && logv[0].runMin >= 100, "saved drive has max and run time");
  CHECK(driveNum == num + 1, "drive number advances");
  for (int i = 0; i < 12; i++) { DriveRec r; memset(&r, 0, sizeof r); r.num = 100 + i; pushLog(r); }
  CHECK(logCount == LOG_N && logv[0].num == 111, "log keeps newest 10");
  saveLog(); loadLog(); CHECK(logCount == LOG_N && logv[9].num == 102, "log survives save/load");
  // clear log with confirm
  go(S_SET); active = true; setSel = ST_CLEARLOG; lastInput = g_ms;
  run(100, 800, 0, true); CHECK(confirm && confirmSel == 0, "CLEAR LOG asks, NO first");
  run(100, 800, 0, true); CHECK(!confirm && logCount == LOG_N, "NO keeps log");
  run(100, 800, 0, true); run(100, 800, 1); run(100, 800, 0, true); CHECK(logCount == 0 && !prefs.isKey("cur"), "YES clears log");
  // millis rollover during PK HOLD
  g_ms = 0xFFFFFFFFu - 4000; go(S_MAIN); lastInput = g_ms;
  run(2000, 1000); run(2000, 1150); CHECK(peakF > 1140, "peak before wrap");
  run(20000, 1000); CHECK(peakF > 1140, "peak held across millis wrap");
  run(12000, 1000); CHECK(fabs(peakF - 1000) < 2, "peak drops after hold across wrap");
  // fault
  processSample(NAN, true, g_ms); CHECK(fault && !alarmOn && band == 0, "fault clears alarm/band");
  printf("\n%s: %d failure(s)\n", fails ? "FAILED" : "ALL PASSED", fails);
  return fails ? 1 : 0;
}
// incremental MAIN redraw must give the same pixels as a full redraw
int incrementalCheck() {
  defaults(); std::vector<uint16_t> a, b;
  struct St { float e, pk; Cool c; } seq[] = {{950, 1100, CD_NONE}, {1010, 1150, CD_NONE}, {880, 1150, CD_NEED}, {640, 700, CD_NEED}, {290, 300, CD_OK}, {150, 300, CD_OK}, {700, 1150, CD_NONE}, {700, 720, CD_NONE}, {1100, 1190, CD_NONE}, {1100, 1110, CD_NONE}, {1180, 1190, CD_NEED}, {0, 0, CD_NONE}};
  int bad = 0;
  scr = S_MAIN; setEgt(seq[0].e, seq[0].pk); cool = seq[0].c; draw();
  for (auto &q : seq) {
    setEgt(q.e, q.pk); cool = q.c; dirty = false; drawMain();      // incremental
    a.assign(tft.getBuffer(), tft.getBuffer() + 240 * 240);
    draw();                                                        // full
    b.assign(tft.getBuffer(), tft.getBuffer() + 240 * 240);
    int diff = 0; for (int i = 0; i < 240 * 240; i++) if (a[i] != b[i]) { diff++; printf("   (%d,%d) inc=%04x full=%04x\n", i % 240, i / 240, a[i], b[i]); }
    printf("incremental vs full at %.0f/%.0f: %d px differ\n", q.e, q.pk, diff); bad += diff > 0;
  }
  return bad;
}
int main(int argc, char **argv) {
  if (argc > 1 && std::string(argv[1]) == "--incr") return incrementalCheck();
  if (argc > 1 && std::string(argv[1]) == "--test") return tests();
  if (argc > 1) outDir = argv[1];
  return renders();
}
