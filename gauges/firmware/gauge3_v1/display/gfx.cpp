// gfx.cpp - see gfx.h
#include "gfx.h"
#include <math.h>
#include <string.h>

static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

uint16_t gfx_blend(uint16_t d, uint16_t s, uint8_t a) {
  if (a >= 250) return s;
  if (a <= 4) return d;
  uint32_t a5 = (a + 4) >> 3;  // 0..32
  uint32_t dd = (d | ((uint32_t)d << 16)) & 0x07E0F81F;
  uint32_t ss = (s | ((uint32_t)s << 16)) & 0x07E0F81F;
  uint32_t r = ((((ss - dd) * a5) >> 5) + dd) & 0x07E0F81F;
  return (uint16_t)(r | (r >> 16));
}
uint16_t gfx_scale(uint16_t c, uint8_t k) { return gfx_blend(0, c, k); }

static inline void plot(Canvas& cv, int x, int y, uint16_t c, float cov) {
  if ((unsigned)x >= (unsigned)cv.w || (unsigned)y >= (unsigned)cv.h || cov <= 0.02f) return;
  uint16_t* p = &cv.px[y * cv.w + x];
  *p = cov >= 0.98f ? c : gfx_blend(*p, c, (uint8_t)(cov * 255));
}

void gfx_clear(Canvas& cv, uint16_t c) {
  if (c == 0) { memset(cv.px, 0, (size_t)cv.w * cv.h * 2); return; }
  for (int i = 0; i < cv.w * cv.h; i++) cv.px[i] = c;
}

void gfx_fill_rect(Canvas& cv, int x, int y, int w, int h, uint16_t c) {
  int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
  int x1 = x + w > cv.w ? cv.w : x + w, y1 = y + h > cv.h ? cv.h : y + h;
  for (int yy = y0; yy < y1; yy++) { uint16_t* p = &cv.px[yy * cv.w]; for (int xx = x0; xx < x1; xx++) p[xx] = c; }
}

// signed distance to a rounded rectangle (negative inside)
static float rr_dist(float px, float py, float x, float y, float w, float h, float r) {
  float cx = x + w * 0.5f, cy = y + h * 0.5f;
  float qx = fabsf(px - cx) - (w * 0.5f - r), qy = fabsf(py - cy) - (h * 0.5f - r);
  float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
  float in = qx > qy ? qx : qy; if (in > 0) in = 0;
  return sqrtf(ox * ox + oy * oy) + in - r;
}
void gfx_round_rect(Canvas& cv, int x, int y, int w, int h, float r, uint16_t c) {
  for (int yy = y - 1; yy <= y + h; yy++)
    for (int xx = x - 1; xx <= x + w; xx++) {
      float d = rr_dist(xx + 0.5f, yy + 0.5f, x, y, w, h, r);
      plot(cv, xx, yy, c, clamp01(0.5f - d));
    }
}
void gfx_round_rect_outline(Canvas& cv, int x, int y, int w, int h, float r, float t, uint16_t c) {
  for (int yy = y - 1; yy <= y + h; yy++)
    for (int xx = x - 1; xx <= x + w; xx++) {
      float d = rr_dist(xx + 0.5f, yy + 0.5f, x, y, w, h, r);
      plot(cv, xx, yy, c, clamp01(0.5f - d) * clamp01(d + t + 0.5f));
    }
}

float gfx_atan2_deg(float y, float x) {
  float ax = fabsf(x), ay = fabsf(y);
  float mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
  if (mx < 1e-6f) return 0;
  float a = mn / mx, s = a * a;
  float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
  if (ay > ax) r = 1.57079637f - r;
  if (x < 0) r = 3.14159274f - r;
  if (y < 0) r = -r;
  float d = r * 57.2957795f;
  return d < 0 ? d + 360.f : d;
}

// Iterate pixels of the annulus [r0-1, r1+1]; fn(x, y, r, angle_deg).
template <typename F>
static void annulus(Canvas& cv, float cx, float cy, float r0, float r1, F fn) {
  float ro = r1 + 1.f, ri = r0 - 1.f; if (ri < 0) ri = 0;
  int ya = (int)floorf(cy - ro), yb = (int)ceilf(cy + ro);
  if (ya < 0) ya = 0;
  if (yb > cv.h - 1) yb = cv.h - 1;
  for (int y = ya; y <= yb; y++) {
    float dy = y + 0.5f - cy;
    float wo2 = ro * ro - dy * dy; if (wo2 <= 0) continue;
    float wo = sqrtf(wo2);
    float wi2 = ri * ri - dy * dy; float wi = wi2 > 0 ? sqrtf(wi2) : -1.f;
    int xa = (int)floorf(cx - wo), xb = (int)ceilf(cx + wo);
    if (xa < 0) xa = 0;
    if (xb > cv.w - 1) xb = cv.w - 1;
    for (int x = xa; x <= xb; x++) {
      float dx = x + 0.5f - cx;
      if (wi > 0 && fabsf(dx) < wi - 1.f) { x = (int)floorf(cx + wi - 1.f); continue; }
      float r = sqrtf(dx * dx + dy * dy);
      if (r < r0 - 1.f || r > r1 + 1.f) continue;
      fn(x, y, r, dx, dy);
    }
  }
}

void gfx_ring(Canvas& cv, float cx, float cy, float r0, float r1, uint16_t c) {
  annulus(cv, cx, cy, r0, r1, [&](int x, int y, float r, float, float) {
    plot(cv, x, y, c, clamp01(r - r0 + 0.5f) * clamp01(r1 - r + 0.5f));
  });
}
void gfx_disc(Canvas& cv, float cx, float cy, float r, uint16_t c) {
  int ya = (int)(cy - r - 1), yb = (int)(cy + r + 1);
  for (int y = ya; y <= yb; y++) {
    if (y < 0 || y >= cv.h) continue;
    float dy = y + 0.5f - cy; float w2 = (r + 1) * (r + 1) - dy * dy; if (w2 <= 0) continue;
    float w = sqrtf(w2);
    for (int x = (int)(cx - w); x <= (int)(cx + w); x++) {
      float dx = x + 0.5f - cx; float d = sqrtf(dx * dx + dy * dy);
      plot(cv, x, y, c, clamp01(r - d + 0.5f));
    }
  }
}

void gfx_arc(Canvas& cv, float cx, float cy, float r0, float r1, float a0, float a1, uint16_t c) {
  float sweep = a1 - a0; if (sweep <= 0) return;
  if (sweep >= 360.f) { gfx_ring(cv, cx, cy, r0, r1, c); return; }
  float st = fmodf(a0, 360.f); if (st < 0) st += 360.f;
  annulus(cv, cx, cy, r0, r1, [&](int x, int y, float r, float dx, float dy) {
    float a = gfx_atan2_deg(dy, dx) - st; if (a < 0) a += 360.f;
    float k = 0.0174533f * r;                     // px per degree
    float ca;
    if (a <= sweep) ca = clamp01(a * k + 0.5f) * clamp01((sweep - a) * k + 0.5f);
    else { float d = fminf(a - sweep, 360.f - a); ca = clamp01(0.5f - d * k); }
    plot(cv, x, y, c, ca * clamp01(r - r0 + 0.5f) * clamp01(r1 - r + 0.5f));
  });
}

void gfx_seg_arc(Canvas& cv, const SegArc& s) {
  float rmax = s.r1;
  for (int z = 0; z < 3; z++) if (s.r1_zone[z] > rmax) rmax = s.r1_zone[z];
  float span = s.sweep / s.n;
  float st = fmodf(s.start, 360.f); if (st < 0) st += 360.f;
  annulus(cv, s.cx, s.cy, s.r0, rmax, [&](int x, int y, float r, float dx, float dy) {
    float a = gfx_atan2_deg(dy, dx) - st; if (a < 0) a += 360.f;
    if (a > s.sweep) return;
    int idx = (int)(a / span); if (idx >= s.n) idx = s.n - 1;
    float f = a - idx * span;                       // degrees into this segment
    float k = 0.0174533f * r;
    float half = s.gap * 0.5f;
    float ca = clamp01((f - half) * k + 0.5f) * clamp01((span - half - f) * k + 0.5f);
    int z = idx >= s.alarm_from ? 2 : (idx >= s.warn_from ? 1 : 0);
    float r1 = s.r1_zone[z] > 0 ? s.r1_zone[z] : s.r1;
    float cr = clamp01(r - s.r0 + 0.5f) * clamp01(r1 - r + 0.5f);
    plot(cv, x, y, idx < s.lit ? s.on[z] : s.off[z], ca * cr);
  });
}

void gfx_line(Canvas& cv, float x0, float y0, float x1, float y1, float wdt, uint16_t c) {
  float hw = wdt * 0.5f;
  int xa = (int)floorf(fminf(x0, x1) - hw - 1), xb = (int)ceilf(fmaxf(x0, x1) + hw + 1);
  int ya = (int)floorf(fminf(y0, y1) - hw - 1), yb = (int)ceilf(fmaxf(y0, y1) + hw + 1);
  float vx = x1 - x0, vy = y1 - y0, l2 = vx * vx + vy * vy; if (l2 < 1e-6f) l2 = 1e-6f;
  for (int y = ya; y <= yb; y++)
    for (int x = xa; x <= xb; x++) {
      float px = x + 0.5f - x0, py = y + 0.5f - y0;
      float t = (px * vx + py * vy) / l2; t = t < 0 ? 0 : (t > 1 ? 1 : t);
      float ex = px - t * vx, ey = py - t * vy;
      float d = sqrtf(ex * ex + ey * ey);
      plot(cv, x, y, c, clamp01(hw - d + 0.5f));
    }
}

void gfx_tri(Canvas& cv, float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c) {
  int xa = (int)floorf(fminf(x0, fminf(x1, x2))) - 1, xb = (int)ceilf(fmaxf(x0, fmaxf(x1, x2))) + 1;
  int ya = (int)floorf(fminf(y0, fminf(y1, y2))) - 1, yb = (int)ceilf(fmaxf(y0, fmaxf(y1, y2))) + 1;
  float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
  float sgn = area < 0 ? -1.f : 1.f;
  float ex[3] = {x0, x1, x2}, ey[3] = {y0, y1, y2};
  for (int y = ya; y <= yb; y++)
    for (int x = xa; x <= xb; x++) {
      float px = x + 0.5f, py = y + 0.5f, cov = 1.f;
      for (int i = 0; i < 3; i++) {
        float ax = ex[i], ay = ey[i], bx = ex[(i + 1) % 3], by = ey[(i + 1) % 3];
        float lx = bx - ax, ly = by - ay, ll = sqrtf(lx * lx + ly * ly); if (ll < 1e-6f) continue;
        float d = sgn * ((px - ax) * ly - (py - ay) * lx) / ll;   // >0 outside
        cov *= clamp01(0.5f - d);
      }
      plot(cv, x, y, c, cov);
    }
}

static const Glyph* glyph(const Font& f, char ch) {
  unsigned u = (unsigned char)ch; if (u < 32 || u > 127) u = '?';
  return &f.g[u - 32];
}
int gfx_text_w(const Font& f, const char* s) {
  int w = 0; for (; *s; s++) w += glyph(f, *s)->adv; return w;
}
void gfx_text(Canvas& cv, const Font& f, int x, int base, const char* s, uint16_t c, int align) {
  if (align == AL_CENTER) x -= gfx_text_w(f, s) / 2;
  else if (align == AL_RIGHT) x -= gfx_text_w(f, s);
  for (; *s; s++) {
    const Glyph* g = glyph(f, *s);
    const uint8_t* b = f.bmp + g->off;
    int n = 0;
    for (int yy = 0; yy < g->h; yy++)
      for (int xx = 0; xx < g->w; xx++, n++) {
        uint8_t v = b[n >> 1]; v = (n & 1) ? (v & 15) : (v >> 4);
        if (v) plot(cv, x + g->xo + xx, base + g->yo + yy, c, v / 15.f);
      }
    x += g->adv;
  }
}
