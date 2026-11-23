// gfx.h - tiny software renderer for a 480x480 RGB565 frame buffer.
// Pure C++ (no Arduino calls) so the same code runs on the ESP32-S3 and on a
// PC to render the PNG previews in ../previews/.
#pragma once
#include <stdint.h>

struct Glyph { uint32_t off; int16_t w, h, xo, yo, adv; };
struct Font  { const uint8_t* bmp; const Glyph* g; int16_t ascent, descent; };
struct Canvas { uint16_t* px; int w, h; };

static inline uint16_t RGB(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
uint16_t gfx_scale(uint16_t c, uint8_t k);            // c * k/255 (dim a colour)
uint16_t gfx_blend(uint16_t dst, uint16_t src, uint8_t a);

void gfx_clear(Canvas& cv, uint16_t c);
void gfx_fill_rect(Canvas& cv, int x, int y, int w, int h, uint16_t c);
void gfx_round_rect(Canvas& cv, int x, int y, int w, int h, float r, uint16_t c);
void gfx_round_rect_outline(Canvas& cv, int x, int y, int w, int h, float r, float t, uint16_t c);
void gfx_disc(Canvas& cv, float cx, float cy, float r, uint16_t c);
void gfx_ring(Canvas& cv, float cx, float cy, float r0, float r1, uint16_t c);
// Angles in degrees, 0 = 3 o'clock, positive = clockwise. a1 > a0.
void gfx_arc(Canvas& cv, float cx, float cy, float r0, float r1, float a0, float a1, uint16_t c);
void gfx_line(Canvas& cv, float x0, float y0, float x1, float y1, float width, uint16_t c);
void gfx_tri(Canvas& cv, float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c);

// Segmented gauge arc. Zone 0 = normal, 1 = warn, 2 = alarm.
struct SegArc {
  float cx, cy, r0, r1;          // inner / outer radius of the normal segments
  float r1_zone[3];              // outer radius per zone (shape cue: taller = worse)
  float start, sweep;            // degrees
  int n;                         // number of segments
  float gap;                     // gap between segments, degrees
  int lit;                       // number of lit segments (0..n)
  int warn_from, alarm_from;     // first segment index of warn / alarm zone (n = none)
  uint16_t on[3], off[3];        // lit / unlit colour per zone
};
void gfx_seg_arc(Canvas& cv, const SegArc& s);

enum { AL_LEFT = 0, AL_CENTER = 1, AL_RIGHT = 2 };
int  gfx_text_w(const Font& f, const char* s);
void gfx_text(Canvas& cv, const Font& f, int x, int baseline, const char* s, uint16_t c, int align);
float gfx_atan2_deg(float y, float x);   // 0..360, clockwise on screen
