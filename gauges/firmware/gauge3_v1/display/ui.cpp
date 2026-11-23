// ui.cpp - gauge screens. Colour-blind safe design (designed for red-green colour blindness):
//   * NO red/green. Palette = blue (normal), amber/yellow (warn), white (alarm / focus).
//   * Every state also has a SHAPE or TEXT cue: warn/alarm zones have taller segments,
//     WARN / HIGH / LOW text pills, alarm = blinking white inner disc, focus = white outer ring,
//     EBP mode = inverted (black-on-white) label.
#include "ui.h"
#include "fonts.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const float CX = 240, CY = 240;
static const uint16_t C_BG     = 0x0000;
static const uint16_t C_WHITE  = 0xFFFF;
#define C_NORMAL  RGB(40, 170, 255)
#define C_WARN    RGB(255, 196, 0)
#define C_GRAY    RGB(150, 158, 168)
#define C_DGRAY   RGB(70, 74, 80)
#define C_OFF0    RGB(24, 36, 52)
#define C_OFF1    RGB(66, 52, 0)
#define C_OFF2    RGB(78, 78, 78)
#define C_SKY     RGB(10, 40, 90)
#define C_GROUND  RGB(52, 44, 34)

static const float ARC_START = 135.f, ARC_SWEEP = 270.f;
static const int   ARC_N = 36;

static bool blink(const UiLocal& L, int period_ms = 500) { return (int)(L.now_ms % (uint32_t)period_ms) < (period_ms * 6 / 10); }
static bool vld(const G3Packet& p, int v) { return (p.valid >> v) & 1; }
static float sv(const G3Packet& p, int s) { return p.set_val[s]; }

static void fmt_num(char* b, size_t n, float v, int dec, bool commas = false) {
  if (!commas) { snprintf(b, n, "%.*f", dec, (double)v); return; }
  long iv = lroundf(v); bool neg = iv < 0; if (neg) iv = -iv;
  char t[24]; snprintf(t, sizeof t, "%ld", iv);
  int len = strlen(t), o = 0; if (neg) b[o++] = '-';
  for (int i = 0; i < len && o < (int)n - 2; i++) { b[o++] = t[i]; int rem = len - 1 - i; if (rem && rem % 3 == 0) b[o++] = ','; }
  b[o] = 0;
}
static float frac(float v, float mn, float mx) { float f = (v - mn) / (mx - mn); return f < 0 ? 0 : (f > 1 ? 1 : f); }
static float val_angle(float v, float mn, float mx) { return ARC_START + ARC_SWEEP * frac(v, mn, mx); }

static void pill(Canvas& cv, int cx, int y, const Font& f, const char* s, uint16_t bg, uint16_t fg, bool outline = false) {
  int w = gfx_text_w(f, s) + 28, h = f.ascent + 6;
  if (outline) gfx_round_rect_outline(cv, cx - w / 2, y, w, h, h / 2.f, 3, bg);
  else gfx_round_rect(cv, cx - w / 2, y, w, h, h / 2.f, bg);
  gfx_text(cv, f, cx, y + h - (h - f.ascent) / 2 - f.descent / 2 - 2, s, fg, AL_CENTER);
}

// main segmented arc. warn/alarm in value units (use > mx for "none")
static void main_arc(Canvas& cv, float v, bool valid, float mn, float mx, float warn, float alarm, bool dim = false) {
  SegArc s;
  s.cx = CX; s.cy = CY; s.r0 = 194; s.r1 = 220;
  s.r1_zone[0] = 220; s.r1_zone[1] = 225; s.r1_zone[2] = 231;
  s.start = ARC_START; s.sweep = ARC_SWEEP; s.n = ARC_N; s.gap = 1.8f;
  s.lit = valid ? (int)ceilf(frac(v, mn, mx) * ARC_N - 0.001f) : 0;
  s.warn_from  = warn  <= mx ? (int)floorf(frac(warn, mn, mx) * ARC_N) : ARC_N;
  s.alarm_from = alarm <= mx ? (int)floorf(frac(alarm, mn, mx) * ARC_N) : ARC_N;
  s.on[0] = C_NORMAL; s.on[1] = C_WARN; s.on[2] = C_WHITE;
  s.off[0] = C_OFF0; s.off[1] = C_OFF1; s.off[2] = C_OFF2;
  if (dim) for (int z = 0; z < 3; z++) { s.on[z] = gfx_scale(s.on[z], 110); s.off[z] = gfx_scale(s.off[z], 140); }
  gfx_seg_arc(cv, s);
}
static void arc_end_labels(Canvas& cv, const char* lo, const char* hi) {
  gfx_text(cv, FONT_S, 112, 392, lo, C_GRAY, AL_CENTER);
  gfx_text(cv, FONT_S, 368, 392, hi, C_GRAY, AL_CENTER);
}
// white target tick for COMMANDED value (or pedal)
static void cmd_tick(Canvas& cv, float v, float mn, float mx) {
  float a = val_angle(v, mn, mx) * 0.0174533f, ca = cosf(a), sa = sinf(a);
  gfx_line(cv, CX + ca * 186, CY + sa * 186, CX + ca * 233, CY + sa * 233, 6, C_BG);   // black outline
  gfx_line(cv, CX + ca * 188, CY + sa * 188, CX + ca * 231, CY + sa * 231, 3.5f, C_WHITE);
  float px = -sa, py = ca;   // perpendicular
  gfx_tri(cv, CX + ca * 190, CY + sa * 190, CX + ca * 174 + px * 9, CY + sa * 174 + py * 9,
          CX + ca * 174 - px * 9, CY + sa * 174 - py * 9, C_WHITE);
}
static void focus_ring(Canvas& cv, const G3Packet& p, const UiLocal& L) {
  bool f = p.focus == FOCUS_ALL || p.focus == L.role;
  if (f) { gfx_ring(cv, CX, CY, 233, 240, C_WHITE); }
}
static void top_tags(Canvas& cv, const G3Packet& p) {
  if (p.flags & F_TEST_MODE) {   // black backing cuts a clean gap in the arc so the tag never overlaps segments
    const char* t = (p.flags & F_SWEEP) ? "TEST SWEEP" : "TEST";
    int w = gfx_text_w(FONT_S, t) + 28, h = FONT_S.ascent + 6;
    gfx_round_rect(cv, 240 - w / 2 - 6, 34, w + 12, h + 12, (h + 12) / 2.f, C_BG);
    pill(cv, 240, 40, FONT_S, t, C_WARN, C_WARN, true);
  }
  else if (!(p.flags & F_CAN_OK)) gfx_text(cv, FONT_S, 240, 66, "NO CAN", C_GRAY, AL_CENTER);
}
static void toast(Canvas& cv, const UiLocal& L) {
  if (L.now_ms < L.toast_until && L.toast[0]) pill(cv, 240, 404, FONT_M, L.toast, C_WHITE, C_BG);
}

// Alarm background: blinking white inner disc (not muted) or a thick white ring (muted).
static void alarm_disc(Canvas& cv, const UiLocal& L, const G3Packet& p, int state, bool valid) {
  if (state != 2 || !valid) return;
  if (p.flags & F_MUTED) gfx_ring(cv, CX, CY, 170, 182, C_WHITE);
  else if (blink(L, 400)) gfx_disc(cv, CX, CY, 182, C_WHITE);
}
// Big centre readout. state: 0 normal, 1 warn, 2 alarm
static void big_readout(Canvas& cv, const UiLocal& L, const G3Packet& p, const char* num, const Font& f, int state, bool valid) {
  bool inv = state == 2 && valid && !(p.flags & F_MUTED) && blink(L, 400);
  uint16_t col = !valid ? C_DGRAY : inv ? C_BG : (state == 1 ? C_WARN : C_WHITE);
  gfx_text(cv, f, 240, 300, valid ? num : "--", col, AL_CENTER);
}
static void status_pill(Canvas& cv, const UiLocal& L, const G3Packet& p, int state, bool valid, const char* hi_word, int y = 142) {
  bool inv = state == 2 && !(p.flags & F_MUTED) && blink(L, 400);
  if (!valid) return;
  if (state == 1) pill(cv, 240, y, FONT_M, "WARN", C_WARN, C_BG);
  else if (state == 2) {
    if (p.flags & F_MUTED) pill(cv, 240, y, FONT_M, "MUTED", C_WHITE, C_WHITE, true);
    else pill(cv, 240, y, FONT_M, hi_word, inv ? C_BG : C_WHITE, inv ? C_WHITE : C_BG);
  }
}

// ------------------------------------------------------------ history graph
static void hist_graph(Canvas& cv, const UiLocal& L, int x, int y, int w, int h, float mn, float mx, float warn, float alarm) {
  gfx_round_rect_outline(cv, x, y, w, h, 8, 2, C_DGRAY);
  auto ypos = [&](float v) { return y + h - 4 - frac(v, mn, mx) * (h - 8); };
  if (warn <= mx) gfx_line(cv, x + 4, ypos(warn), x + w - 4, ypos(warn), 1.5f, C_WARN);
  if (alarm <= mx) for (int xx = x + 4; xx < x + w - 8; xx += 12) gfx_line(cv, xx, ypos(alarm), xx + 6, ypos(alarm), 2, C_WHITE);
  int n = L.hist_n; if (n < 2) { gfx_text(cv, FONT_S, x + w / 2, y + h / 2 + 8, "COLLECTING...", C_GRAY, AL_CENTER); return; }
  float px = 0, py = 0;
  for (int i = 0; i < n; i++) {
    int idx = (L.hist_head - n + i + UI_HIST_N) % UI_HIST_N;
    float xx = x + 4 + (float)(w - 8) * i / (UI_HIST_N - 1), yy = ypos(L.hist[idx]);
    if (i) gfx_line(cv, px, py, xx, yy, 2.5f, C_NORMAL);
    px = xx; py = yy;
  }
  gfx_text(cv, FONT_S, x + w - 6, y + h + 26, "NOW", C_GRAY, AL_RIGHT);
  gfx_text(cv, FONT_S, x + 6, y + h + 26, "-2 MIN", C_GRAY, AL_LEFT);
}

// ------------------------------------------------------------ settings page
static void settings_page(Canvas& cv, const G3Packet& p, const UiLocal& L, const char* gname) {
  main_arc(cv, 0, false, 0, 1, 2, 2, true);
  gfx_text(cv, FONT_M, 240, 112, "SETTINGS", C_WHITE, AL_CENTER);
  gfx_text(cv, FONT_S, 240, 142, gname, C_GRAY, AL_CENTER);
  int n = g3_settings_count(L.role);
  int sel = p.set_idx < n ? p.set_idx : n - 1;
  bool edit = (p.flags & F_SET_EDIT) && (p.focus == L.role);
  int first = sel - 1; if (first > n - 3) first = n - 3; if (first < 0) first = 0;
  for (int r = 0; r < 3 && first + r < n; r++) {
    int item = first + r, id = g3_settings_id(L.role, item);
    const SettingDef& d = G3_SETTINGS[id];
    int y = 168 + r * 64;
    bool is_sel = item == sel;
    float v = (is_sel && edit) ? p.edit_val : sv(p, id);
    char num[24], line[40]; fmt_num(num, sizeof num, v, d.decimals, v >= 10000); snprintf(line, sizeof line, "%s %s", num, d.unit);
    uint16_t fg = C_WHITE;
    if (is_sel && edit) { gfx_round_rect(cv, 64, y, 352, 56, 14, C_WHITE); fg = C_BG; }
    else if (is_sel) gfx_round_rect_outline(cv, 64, y, 352, 56, 14, 3, C_WHITE);
    gfx_text(cv, FONT_S, 84, y + 37, d.name, is_sel ? fg : C_GRAY, AL_LEFT);
    gfx_text(cv, FONT_M, 398, y + 41, line, fg, AL_RIGHT);
  }
  if (n > 3) {   // scroll position dots
    for (int i = 0; i < n; i++) gfx_disc(cv, 428, 220 + (i - (n - 1) / 2.f) * 16, i == sel ? 5 : 3, i == sel ? C_WHITE : C_DGRAY);
  }
  gfx_text(cv, FONT_S, 240, 384, edit ? "TURN = ADJUST   CLICK = SAVE" : "TURN = SCROLL   CLICK = EDIT", C_GRAY, AL_CENTER);
  gfx_text(cv, FONT_S, 240, 414, edit ? "HOLD = CANCEL" : "STARTING VALUES, NOT SPECS", C_DGRAY, AL_CENTER);
}

// ------------------------------------------------------------ gauge 1: EGT
static void g1(Canvas& cv, const G3Packet& p, const UiLocal& L) {
  float mn = 0, mx = 2000, w = sv(p, S_EGT_WARN), a = sv(p, S_EGT_ALARM);
  bool ok = vld(p, V_EGT); float v = p.val[V_EGT];
  int page = p.page[0];
  if (page == PAGE_SETTINGS) { settings_page(cv, p, L, "EGT"); return; }
  if (page == PAGE_PEAK) {
    main_arc(cv, p.peak[V_EGT], true, mn, mx, w, a, true);
    gfx_text(cv, FONT_M, 240, 104, "EGT PEAK", C_WHITE, AL_CENTER);
    char b[16]; fmt_num(b, sizeof b, p.peak[V_EGT], 0);
    gfx_text(cv, FONT_BIG2, 240, 214, b, C_WHITE, AL_CENTER);
    hist_graph(cv, L, 110, 236, 260, 104, 0, 2000, w, a);
    char nb[32], t[16]; fmt_num(t, sizeof t, v, 0); snprintf(nb, sizeof nb, "NOW %s \x7f" "F", ok ? t : "--");
    gfx_text(cv, FONT_M, 240, 410, nb, C_GRAY, AL_CENTER);
    gfx_text(cv, FONT_S, 240, 440, "HOLD = RESET PEAK", C_DGRAY, AL_CENTER);
    arc_end_labels(cv, "0", "2000");
    return;
  }
  int st = !ok ? 0 : (p.alarms & A_EGT_ALARM) ? 2 : (p.alarms & A_EGT_WARN) ? 1 : 0;
  main_arc(cv, v, ok, mn, mx, w, a);
  alarm_disc(cv, L, p, st, ok);
  arc_end_labels(cv, "0", "2000");
  gfx_text(cv, FONT_M, 240, 126, "EGT", st == 2 && !(p.flags & F_MUTED) && blink(L, 400) ? C_BG : C_GRAY, AL_CENTER);
  status_pill(cv, L, p, st, ok, "HIGH");
  char b[16]; fmt_num(b, sizeof b, v, 0);
  big_readout(cv, L, p, b, FONT_BIG, st, ok);
  bool inv = st == 2 && !(p.flags & F_MUTED) && blink(L, 400);
  if (!ok) pill(cv, 240, 142, FONT_M, (p.flags & F_EGT_PROBE) ? "NO DATA" : "PROBE?", C_GRAY, C_GRAY, true);
  gfx_text(cv, FONT_M, 240, 342, "\x7f" "F", inv ? C_BG : C_GRAY, AL_CENTER);
  char pk[32]; fmt_num(b, sizeof b, p.peak[V_EGT], 0); snprintf(pk, sizeof pk, "PEAK %s", b);
  gfx_text(cv, FONT_M, 240, 384, pk, inv ? C_BG : C_WHITE, AL_CENTER);
}

// ------------------------------------------------------------ gauge 2: BOOST / EXH BRAKE
// v2 (Nov 22 2026 drive log): BOOST 0-40 psi under throttle; EXH BRAKE mode (exhaust brake working,
// CAN-C 0x30A b7 > 0) shows real exhaust back pressure (PID 73) 0-80 psi, with BRAKE HP underneath.
#define G2_BOOST_MAX 40.f
#define G2_EBP_MAX   80.f
static void g2(Canvas& cv, const G3Packet& p, const UiLocal& L) {
  int page = p.page[1];
  bool ebp_mode = p.flags & F_G2_EBP;
  if (page == PAGE_SETTINGS) { settings_page(cv, p, L, "BOOST / EXH BRAKE"); return; }
  if (page == PAGE_PEAK) {
    main_arc(cv, p.peak[V_BOOST], true, 0, G2_BOOST_MAX, sv(p, S_BOOST_WARN), sv(p, S_BOOST_ALARM), true);
    gfx_text(cv, FONT_M, 240, 104, "PEAKS", C_WHITE, AL_CENTER);
    char b[16], l[24];
    gfx_text(cv, FONT_M, 104, 164, "BOOST", C_GRAY, AL_LEFT);
    fmt_num(b, sizeof b, p.peak[V_BOOST], 1); snprintf(l, sizeof l, "%s PSI", b);
    gfx_text(cv, FONT_M, 376, 164, l, C_WHITE, AL_RIGHT);
    gfx_text(cv, FONT_M, 104, 210, "EBP", C_GRAY, AL_LEFT);
    if (p.flags & F_EBP_SUPPORTED) { fmt_num(b, sizeof b, p.peak[V_EBP], 1); snprintf(l, sizeof l, "%s PSI", b); } else strcpy(l, "N/A");
    gfx_text(cv, FONT_M, 376, 210, l, C_WHITE, AL_RIGHT);
    gfx_text(cv, FONT_M, 104, 256, "BRAKE", C_GRAY, AL_LEFT);
    fmt_num(b, sizeof b, p.peak[V_BRAKE_HP], 0); snprintf(l, sizeof l, "%s HP", b);
    gfx_text(cv, FONT_M, 376, 256, l, C_WHITE, AL_RIGHT);
    hist_graph(cv, L, 110, 274, 260, 74, 0, G2_BOOST_MAX, sv(p, S_BOOST_WARN), sv(p, S_BOOST_ALARM));
    gfx_text(cv, FONT_S, 240, 412, "HOLD = RESET PEAKS", C_DGRAY, AL_CENTER);
    return;
  }
  int vid = ebp_mode ? V_EBP : V_BOOST;
  float mx = ebp_mode ? G2_EBP_MAX : G2_BOOST_MAX;
  float w = sv(p, ebp_mode ? S_EBP_WARN : S_BOOST_WARN), a = sv(p, ebp_mode ? S_EBP_ALARM : S_BOOST_ALARM);
  bool ok = vld(p, vid); float v = p.val[vid];
  uint32_t abit = ebp_mode ? A_EBP_ALARM : A_BOOST_ALARM, wbit = ebp_mode ? A_EBP_WARN : A_BOOST_WARN;
  // an alarm on the hidden value still shows (ex: high drive pressure under boost)
  bool other_alarm = (p.alarms & (ebp_mode ? A_BOOST_ALARM : A_EBP_ALARM)) != 0;
  int st = !ok ? 0 : (p.alarms & abit) ? 2 : (p.alarms & wbit) ? 1 : 0;
  main_arc(cv, v, ok, 0, mx, w, a);
  alarm_disc(cv, L, p, st, ok);
  arc_end_labels(cv, "0", ebp_mode ? "80" : "40");
  bool inv = st == 2 && !(p.flags & F_MUTED) && blink(L, 400);
  // mode label: BOOST = plain white text; EXH BRAKE = inverted black-on-white box (brightness + shape cue)
  if (ebp_mode) pill(cv, 240, 96, FONT_M, "EXH BRAKE", inv ? C_BG : C_WHITE, inv ? C_WHITE : C_BG);
  else gfx_text(cv, FONT_L, 240, 130, "BOOST", inv ? C_BG : C_WHITE, AL_CENTER);
  if (st) status_pill(cv, L, p, st, ok, "HIGH", 150);
  char b[16]; fmt_num(b, sizeof b, v, 1);
  big_readout(cv, L, p, b, FONT_BIG, st, ok);
  gfx_text(cv, FONT_M, 240, 342, ebp_mode ? "PSI  EBP" : "PSI", inv ? C_BG : C_GRAY, AL_CENTER);
  char sm[40], t[16];
  if (ebp_mode) {                       // secondary: braking power, same 0-230 HP number as the dash
    fmt_num(t, sizeof t, p.val[V_BRAKE_HP], 0);
    snprintf(sm, sizeof sm, "BRAKE %s HP", vld(p, V_BRAKE_HP) ? t : "--");
  } else if (!(p.flags & F_EBP_SUPPORTED)) snprintf(sm, sizeof sm, "EBP N/A");
  else { fmt_num(t, sizeof t, p.val[V_EBP], 0); snprintf(sm, sizeof sm, "EBP %s", vld(p, V_EBP) ? t : "--"); }
  if (other_alarm) {                    // hidden value in alarm: show it boxed, with its own word
    if (ebp_mode) { fmt_num(t, sizeof t, p.val[V_BOOST], 1); snprintf(sm, sizeof sm, "BOOST %s HIGH", t); }
    else { fmt_num(t, sizeof t, p.val[V_EBP], 0); snprintf(sm, sizeof sm, "EBP %s HIGH", t); }
    pill(cv, 240, 356, FONT_M, sm, C_WHITE, C_BG);
  }
  else gfx_text(cv, FONT_M, 240, 386, sm, inv ? C_BG : C_GRAY, AL_CENTER);
}

// ------------------------------------------------------------ gauge 3: utility
static void page_dots(Canvas& cv, int cur) {
  for (int i = 0; i < UP_COUNT; i++) {
    float x = 240 + (i - (UP_COUNT - 1) / 2.f) * 20;
    if (i == cur) gfx_disc(cv, x, 436, 6, C_WHITE); else gfx_disc(cv, x, 436, 3.5f, C_GRAY);
  }
}
// RAIL PRESSURE: actual = arc + big number, commanded = white tick + CMD line.
// Segments from 27,000 psi up are taller and pale (red-zone marker by SHAPE, not colour).
#define G3_RAIL_MAX  30000.f
#define G3_RAIL_ZONE 27000.f   // Torque Pro 6.7 PID pack max (reference, not an OEM spec)
static void rail_page(Canvas& cv, const G3Packet& p, const UiLocal& L) {
  const float mn = 0, mx = G3_RAIL_MAX;
  bool ok = vld(p, V_FRP_ACT), okc = vld(p, V_FRP_CMD);
  bool lag = (p.alarms & A_FRP_MM) && ok, low = (p.alarms & A_FRP_LOW) && ok;
  bool mm = lag || low;
  bool muted = p.flags & F_MUTED;
  bool bl = mm && !muted && blink(L, 400);
  float v = p.val[V_FRP_ACT];
  // arc: actual. On an alarm the lit segments turn amber (and blink white if not muted)
  SegArc s;
  s.cx = CX; s.cy = CY; s.r0 = 194; s.r1 = 220; s.r1_zone[0] = s.r1_zone[1] = 220; s.r1_zone[2] = 231;
  s.start = ARC_START; s.sweep = ARC_SWEEP; s.n = ARC_N; s.gap = 1.8f;
  s.lit = ok ? (int)ceilf(frac(v, mn, mx) * ARC_N - 0.001f) : 0;
  s.warn_from = ARC_N; s.alarm_from = (int)floorf(frac(G3_RAIL_ZONE, mn, mx) * ARC_N);
  s.on[0] = mm ? (bl ? C_WHITE : C_WARN) : C_NORMAL; s.off[0] = C_OFF0;
  s.on[1] = s.on[0]; s.off[1] = C_OFF0;
  s.on[2] = C_WHITE; s.off[2] = C_OFF2;
  gfx_seg_arc(cv, s);
  arc_end_labels(cv, "0", "30K");
  if (okc) cmd_tick(cv, p.val[V_FRP_CMD], mn, mx);
  gfx_text(cv, FONT_M, 240, 126, "RAIL PRESSURE", C_GRAY, AL_CENTER);
  if (mm) {
    if (muted) pill(cv, 240, 142, FONT_M, "MUTED", C_WHITE, C_WHITE, true);
    else pill(cv, 240, 142, FONT_M, low ? "RAIL LOW" : "RAIL LAG", bl ? C_WHITE : C_WARN, C_BG);
  }
  char b[20]; fmt_num(b, sizeof b, v, 0, true);
  gfx_text(cv, FONT_BIG2, 240, 290, ok ? b : "--", !ok ? C_DGRAY : (mm ? C_WARN : C_WHITE), AL_CENTER);
  gfx_text(cv, FONT_S, 240, 322, "PSI  ACTUAL", C_GRAY, AL_CENTER);
  char c[20], line[48]; fmt_num(c, sizeof c, p.val[V_FRP_CMD], 0, true);
  snprintf(line, sizeof line, "CMD %s", okc ? c : "N/A");
  // CMD readout: white tick symbol + text (same symbol as the arc marker)
  int tw = gfx_text_w(FONT_M, line);
  gfx_tri(cv, 240 - tw / 2 - 22, 352, 240 - tw / 2 - 10, 360, 240 - tw / 2 - 22, 368, C_WHITE);
  gfx_text(cv, FONT_M, 240 + 6, 372, line, C_WHITE, AL_CENTER);
  if (mm && okc) {
    char d[20], dl[32]; fmt_num(d, sizeof d, v - p.val[V_FRP_CMD], 0, true); snprintf(dl, sizeof dl, "DIFF %s", d);
    gfx_text(cv, FONT_S, 240, 404, dl, C_WARN, AL_CENTER);
  }
}
static void simple_page(Canvas& cv, const G3Packet& p, const UiLocal& L, const char* title, int vid, float mn, float mx,
                        const char* lo, const char* hi, int dec, const char* unit, float warn, bool warn_active, const char* warn_word) {
  bool ok = vld(p, vid); float v = p.val[vid];
  main_arc(cv, v, ok, mn, mx, warn, mx + 1);
  arc_end_labels(cv, lo, hi);
  gfx_text(cv, FONT_M, 240, 126, title, C_GRAY, AL_CENTER);
  if (warn_active && ok) pill(cv, 240, 142, FONT_M, warn_word, C_WARN, C_BG);
  char b[16]; fmt_num(b, sizeof b, v, dec);
  gfx_text(cv, FONT_BIG, 240, 300, ok ? b : "--", !ok ? C_DGRAY : (warn_active ? C_WARN : C_WHITE), AL_CENTER);
  gfx_text(cv, FONT_M, 240, 342, unit, C_GRAY, AL_CENTER);
}
static void tilt_page(Canvas& cv, const G3Packet& p, const UiLocal& L) {
  (void)p;
  float R = 186;
  if (!L.imu_ok) { gfx_text(cv, FONT_L, 240, 260, "IMU N/A", C_GRAY, AL_CENTER); return; }
  float rr = L.roll * 0.0174533f, ppx = L.pitch * 4.0f;   // 4 px per degree
  float nx = -sinf(rr), ny = cosf(rr);   // normal pointing to ground
  // sky / ground disc
  for (int y = (int)(CY - R); y <= (int)(CY + R); y++)
    for (int x = (int)(CX - R); x <= (int)(CX + R); x++) {
      float dx = x + .5f - CX, dy = y + .5f - CY, d2 = dx * dx + dy * dy;
      if (d2 > R * R) continue;
      float side = dx * nx + dy * ny - ppx;
      cv.px[y * cv.w + x] = side > 0 ? C_GROUND : C_SKY;
    }
  // horizon line
  float hx = CX + nx * ppx, hy = CY + ny * ppx, tx = ny, ty = -nx;
  gfx_line(cv, hx - tx * 170, hy - ty * 170, hx + tx * 170, hy + ty * 170, 3, C_WHITE);
  // roll scale (top) and pointer
  for (int d = -40; d <= 40; d += 10) {
    float a = (-90 + d) * 0.0174533f;
    float r0 = (d % 20 == 0) ? 196 : 204;
    gfx_line(cv, CX + cosf(a) * r0, CY + sinf(a) * r0, CX + cosf(a) * 224, CY + sinf(a) * 224, 3, C_WHITE);
  }
  float rl = L.roll; if (rl > 45) rl = 45; if (rl < -45) rl = -45;
  float a = (-90 + rl) * 0.0174533f, ca = cosf(a), sa = sinf(a);
  gfx_tri(cv, CX + ca * 190, CY + sa * 190, CX + ca * 172 - sa * 10, CY + sa * 172 + ca * 10, CX + ca * 172 + sa * 10, CY + sa * 172 - ca * 10, C_WARN);
  // fixed truck reference symbol
  gfx_line(cv, 150, 240, 205, 240, 6, C_WARN); gfx_line(cv, 275, 240, 330, 240, 6, C_WARN);
  gfx_disc(cv, 240, 240, 7, C_WARN);
  char b[32];
  gfx_round_rect(cv, 150, 270, 180, 96, 14, gfx_scale(C_BG, 0));
  snprintf(b, sizeof b, "PITCH %+.1f\x7f", (double)L.pitch); gfx_text(cv, FONT_M, 240, 306, b, C_WHITE, AL_CENTER);
  snprintf(b, sizeof b, "ROLL %+.1f\x7f", (double)L.roll);  gfx_text(cv, FONT_M, 240, 350, b, C_WHITE, AL_CENTER);
  gfx_text(cv, FONT_S, 240, 140, "PITCH / ROLL", C_WHITE, AL_CENTER);
  gfx_text(cv, FONT_S, 240, 400, "HOLD = SET LEVEL", C_GRAY, AL_CENTER);
}
static void g3(Canvas& cv, const G3Packet& p, const UiLocal& L) {
  int page = p.page[2];
  if (page == PAGE_SETTINGS) { settings_page(cv, p, L, "UTILITY"); return; }
  if (page == PAGE_PEAK) {
    main_arc(cv, 0, false, 0, 1, 2, 2, true);
    gfx_text(cv, FONT_M, 240, 104, "PEAKS", C_WHITE, AL_CENTER);
    struct { const char* n; int v; int dec; bool cm; const char* u; } rows[] = {
      {"RAIL", V_FRP_ACT, 0, true, " PSI"}, {"RAIL CMD", V_FRP_CMD, 0, true, " PSI"}, {"LOAD", V_LOAD, 0, false, "%"},
      {"IAT", V_IAT, 0, false, "\x7f" "F"}, {"BATT MIN", V_BATT, 1, false, " V"}};
    for (int i = 0; i < 5; i++) {
      int y = 160 + i * 46; char b[20], l[28];
      fmt_num(b, sizeof b, p.peak[rows[i].v], rows[i].dec, rows[i].cm); snprintf(l, sizeof l, "%s%s", b, rows[i].u);
      gfx_text(cv, FONT_M, 100, y, rows[i].n, C_GRAY, AL_LEFT);
      gfx_text(cv, FONT_M, 380, y, l, C_WHITE, AL_RIGHT);
    }
    gfx_text(cv, FONT_S, 240, 420, "HOLD = RESET PEAKS", C_DGRAY, AL_CENTER);
    return;
  }
  switch (p.util_page) {
    case UP_FRP: rail_page(cv, p, L); break;
    case UP_LOAD: {
      // load arc with white tick = pedal
      bool ok = vld(p, V_LOAD);
      main_arc(cv, p.val[V_LOAD], ok, 0, 100, 101, 101);
      arc_end_labels(cv, "0", "100");
      if (vld(p, V_PEDAL)) cmd_tick(cv, p.val[V_PEDAL], 0, 100);
      gfx_text(cv, FONT_M, 240, 126, "ENGINE LOAD", C_GRAY, AL_CENTER);
      char b[16]; fmt_num(b, sizeof b, p.val[V_LOAD], 0);
      gfx_text(cv, FONT_BIG, 240, 300, ok ? b : "--", ok ? C_WHITE : C_DGRAY, AL_CENTER);
      gfx_text(cv, FONT_M, 240, 342, "%", C_GRAY, AL_CENTER);
      char l[32], t[16]; fmt_num(t, sizeof t, p.val[V_PEDAL], 0); snprintf(l, sizeof l, "PEDAL %s%%", vld(p, V_PEDAL) ? t : "--");
      int tw = gfx_text_w(FONT_M, l);
      gfx_tri(cv, 240 - tw / 2 - 22, 362, 240 - tw / 2 - 10, 370, 240 - tw / 2 - 22, 378, C_WHITE);
      gfx_text(cv, FONT_M, 246, 382, l, C_WHITE, AL_CENTER);
    } break;
    case UP_IAT: simple_page(cv, p, L, "INTAKE AIR", V_IAT, 0, 250, "0", "250", 0, "\x7f" "F", sv(p, S_IAT_WARN), p.alarms & A_IAT_WARN, "HOT"); break;
    case UP_BATT: simple_page(cv, p, L, "BATTERY", V_BATT, 10, 16, "10", "16", 1, "VOLTS", 99, p.alarms & A_BATT_LOW, "LOW"); break;
    case UP_TILT: tilt_page(cv, p, L); break;
  }
  // a mismatch on a page that is not on screen still shows a banner
  if (p.util_page != UP_FRP && (p.alarms & (A_FRP_MM | A_FRP_LOW)))
    pill(cv, 240, 84, FONT_M, (p.alarms & A_FRP_LOW) ? "RAIL LOW!" : "RAIL LAG!", (!(p.flags & F_MUTED) && blink(L, 400)) ? C_WHITE : C_WARN, C_BG);
  page_dots(cv, p.util_page);
}

static void no_signal(Canvas& cv, const UiLocal& L) {
  main_arc(cv, 0, false, 0, 1, 2, 2, true);
  const char* names[] = {"", "GAUGE 1  EGT", "GAUGE 2  BOOST/EXH BRAKE", "GAUGE 3  UTILITY"};
  gfx_text(cv, FONT_S, 240, 150, names[L.role < 1 || L.role > 3 ? 0 : L.role], C_GRAY, AL_CENTER);
  gfx_text(cv, FONT_L, 240, 262, "NO SIGNAL", C_WHITE, AL_CENTER);
  gfx_text(cv, FONT_S, 240, 300, "NO DATA FROM HUB FOR 1 S", C_GRAY, AL_CENTER);
  gfx_text(cv, FONT_S, 240, 330, "CHECK HUB POWER", C_GRAY, AL_CENTER);
  if (blink(L, 1000)) gfx_disc(cv, 240, 380, 7, C_WHITE); else gfx_ring(cv, 240, 380, 5, 7, C_WHITE);
}

void ui_render(Canvas& cv, const G3Packet& p, const UiLocal& L) {
  gfx_clear(cv, C_BG);
  if (!L.link_ok) { no_signal(cv, L); return; }
  if (L.role == 1) g1(cv, p, L); else if (L.role == 2) g2(cv, p, L); else g3(cv, p, L);
  top_tags(cv, p);
  focus_ring(cv, p, L);
  toast(cv, L);
}

void ui_hist_push(UiLocal& L, float v) {
  L.hist[L.hist_head] = v; L.hist_head = (L.hist_head + 1) % UI_HIST_N; if (L.hist_n < UI_HIST_N) L.hist_n++;
}
float ui_hist_value(int role, const G3Packet& p, bool* valid) {
  int v = role == 1 ? V_EGT : role == 2 ? V_BOOST : V_FRP_ACT;
  if (role == 3) { static const int m[UP_COUNT] = {V_FRP_ACT, V_LOAD, V_IAT, V_BATT, V_LOAD}; v = m[p.util_page % UP_COUNT]; }
  if (valid) *valid = vld(p, v);
  return p.val[v];
}

// ================================================================ demo scenarios
// Gauge 2 / 3 values are REAL samples from the Nov 22 2026 v4 drive log
// (obd2-sniff/decode/README.md 'v4 drive'), except the alarm demos and IAT / battery / tilt.
struct Demo { const char* name; int role; };
static const Demo DEMOS[] = {
  {"g1_egt_normal", 1}, {"g1_egt_warn", 1}, {"g1_egt_alarm_blink", 1}, {"g1_egt_alarm_muted", 1},
  {"g1_egt_focused", 1}, {"g1_egt_peak_history", 1}, {"g1_settings_browse", 1}, {"g1_settings_edit", 1},
  {"g1_egt_no_probe", 1}, {"g1_peak_reset_toast", 1},
  {"g2_boost_pull", 2}, {"g2_boost_cruise", 2}, {"g2_exhaust_brake", 2}, {"g2_exhaust_brake_peak_caution", 2},
  {"g2_boost_alarm_demo", 2}, {"g2_peaks", 2}, {"g2_settings", 2}, {"g2_all_focused", 2},
  {"g3_rail_idle", 3}, {"g3_rail_pull", 3}, {"g3_rail_lag_alarm_demo", 3}, {"g3_rail_low_alarm_demo", 3},
  {"g3_load_pedal", 3}, {"g3_intake_air", 3}, {"g3_battery", 3}, {"g3_pitch_roll", 3}, {"g3_peaks", 3}, {"g3_settings", 3},
  {"no_signal", 1},
};
int ui_demo_count() { return sizeof(DEMOS) / sizeof(DEMOS[0]); }
const char* ui_demo_name(int i) { return DEMOS[i].name; }
int ui_demo_role(int i) { return DEMOS[i].role; }

void ui_demo_build(int i, G3Packet& p, UiLocal& L) {
  memset(&p, 0, sizeof p);
  for (int s = 0; s < S_COUNT; s++) p.set_val[s] = G3_SETTINGS[s].def;
  p.flags = F_CAN_OK | F_EGT_PROBE | F_EBP_SUPPORTED | F_EB_BCAST;
  p.valid = 0xFFFF; p.brightness = 80;
  float* v = p.val;
  v[V_EGT] = 985; v[V_BOOST] = 9.0f; v[V_EBP] = 9.0f; v[V_FRP_ACT] = 8000; v[V_FRP_CMD] = 8150;
  v[V_LOAD] = 30; v[V_PEDAL] = 22; v[V_IAT] = 104; v[V_BATT] = 14.2f; v[V_RPM] = 1650; v[V_BRAKE_HP] = 0;
  float* pk = p.peak;   // drive peaks (Nov 22 2026)
  pk[V_EGT] = 1288; pk[V_BOOST] = 32.3f; pk[V_EBP] = 66.0f; pk[V_BRAKE_HP] = 86;
  pk[V_FRP_ACT] = 25524; pk[V_FRP_CMD] = 25696; pk[V_LOAD] = 82; pk[V_PEDAL] = 82; pk[V_RPM] = 2734;
  pk[V_IAT] = 131; pk[V_BATT] = 13.1f;
  L.role = DEMOS[i].role; L.link_ok = true; L.now_ms = 100; L.imu_ok = true; L.pitch = 0; L.roll = 0;
  L.hist_n = 0; L.hist_head = 0; L.toast_until = 0;
  for (int k = 0; k < UI_HIST_N; k++) {   // synthetic 2-minute history (EGT: tow up a grade; boost: pulls to ~32 psi)
    float t = k / (float)UI_HIST_N;
    float h = L.role == 1 ? 700 + 560 * sinf(t * 3.1416f) + 40 * sinf(t * 40)
                          : 4 + 27 * sinf(t * 3.1416f) * sinf(t * 3.1416f) + 1.5f * sinf(t * 31);
    ui_hist_push(L, h);
  }
  const char* n = DEMOS[i].name;
  if (!strcmp(n, "g1_egt_warn")) { v[V_EGT] = 1292; p.alarms = A_EGT_WARN; }
  else if (!strcmp(n, "g1_egt_alarm_blink")) { v[V_EGT] = 1436; pk[V_EGT] = 1436; p.alarms = A_EGT_WARN | A_EGT_ALARM; }
  else if (!strcmp(n, "g1_egt_alarm_muted")) { v[V_EGT] = 1418; pk[V_EGT] = 1436; p.alarms = A_EGT_WARN | A_EGT_ALARM; p.flags |= F_MUTED; }
  else if (!strcmp(n, "g1_egt_focused")) { p.focus = FOCUS_G1; v[V_EGT] = 1105; }
  else if (!strcmp(n, "g1_egt_peak_history")) { p.focus = FOCUS_G1; p.page[0] = PAGE_PEAK; v[V_EGT] = 1105; }
  else if (!strcmp(n, "g1_settings_browse")) { p.focus = FOCUS_G1; p.page[0] = PAGE_SETTINGS; p.set_idx = 0; }
  else if (!strcmp(n, "g1_settings_edit")) { p.focus = FOCUS_G1; p.page[0] = PAGE_SETTINGS; p.set_idx = 1; p.flags |= F_SET_EDIT; p.edit_val = 1450; }
  else if (!strcmp(n, "g1_egt_no_probe")) { p.valid &= ~(1 << V_EGT); p.flags &= ~F_EGT_PROBE; p.flags |= F_TEST_MODE; }
  else if (!strcmp(n, "g1_peak_reset_toast")) { pk[V_EGT] = 1105; v[V_EGT] = 1105; L.toast_until = 2000; strcpy(L.toast, "PEAK RESET"); }
  // gauge 2: P6 pull, cruise, B7 brake, B1 brake peak
  else if (!strcmp(n, "g2_boost_pull")) { v[V_BOOST] = 32.3f; v[V_EBP] = 51.0f; v[V_PEDAL] = 82; v[V_LOAD] = 82; v[V_RPM] = 1980; }
  else if (!strcmp(n, "g2_boost_cruise")) { v[V_BOOST] = 9.0f; v[V_EBP] = 9.0f; v[V_PEDAL] = 22; }
  else if (!strcmp(n, "g2_exhaust_brake")) { p.flags |= F_G2_EBP; v[V_BOOST] = 2.6f; v[V_EBP] = 58.0f; v[V_BRAKE_HP] = 86; v[V_PEDAL] = 0; v[V_RPM] = 1700; }
  else if (!strcmp(n, "g2_exhaust_brake_peak_caution")) { p.flags |= F_G2_EBP; v[V_BOOST] = 2.3f; v[V_EBP] = 66.0f; v[V_BRAKE_HP] = 81; v[V_PEDAL] = 0; p.alarms = A_EBP_WARN; }
  else if (!strcmp(n, "g2_boost_alarm_demo")) { v[V_BOOST] = 38.5f; v[V_EBP] = 55.0f; v[V_PEDAL] = 100; p.alarms = A_BOOST_WARN | A_BOOST_ALARM; }
  else if (!strcmp(n, "g2_peaks")) { p.focus = FOCUS_G2; p.page[1] = PAGE_PEAK; }
  else if (!strcmp(n, "g2_all_focused")) { p.focus = FOCUS_ALL; v[V_BOOST] = 25.8f; v[V_EBP] = 35.8f; p.flags |= F_TEST_MODE | F_SWEEP; }
  else if (!strcmp(n, "g2_settings")) { p.focus = FOCUS_G2; p.page[1] = PAGE_SETTINGS; p.set_idx = 2; }
  // gauge 3: parked idle median, P6 pull, demos
  else if (!strcmp(n, "g3_rail_idle")) { p.util_page = UP_FRP; v[V_FRP_ACT] = 4635; v[V_FRP_CMD] = 4696; v[V_RPM] = 700; }
  else if (!strcmp(n, "g3_rail_pull")) { p.util_page = UP_FRP; v[V_FRP_ACT] = 25500; v[V_FRP_CMD] = 25700; }
  else if (!strcmp(n, "g3_rail_lag_alarm_demo")) { p.util_page = UP_FRP; v[V_FRP_ACT] = 18000; v[V_FRP_CMD] = 21500; p.alarms = A_FRP_MM; }
  else if (!strcmp(n, "g3_rail_low_alarm_demo")) { p.util_page = UP_FRP; v[V_FRP_ACT] = 3600; v[V_FRP_CMD] = 4700; v[V_RPM] = 700; p.alarms = A_FRP_LOW; }
  else if (!strcmp(n, "g3_load_pedal")) { p.util_page = UP_LOAD; v[V_LOAD] = 82; v[V_PEDAL] = 82; }
  else if (!strcmp(n, "g3_intake_air")) { p.util_page = UP_IAT; v[V_IAT] = 118; }
  else if (!strcmp(n, "g3_battery")) { p.util_page = UP_BATT; v[V_BATT] = 14.1f; }
  else if (!strcmp(n, "g3_pitch_roll")) { p.util_page = UP_TILT; L.pitch = 4.5f; L.roll = -6.0f; }
  else if (!strcmp(n, "g3_peaks")) { p.focus = FOCUS_G3; p.page[2] = PAGE_PEAK; }
  else if (!strcmp(n, "g3_settings")) { p.focus = FOCUS_G3; p.page[2] = PAGE_SETTINGS; p.set_idx = 1; }
  else if (!strcmp(n, "no_signal")) { L.link_ok = false; }
}
