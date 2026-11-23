// ui.h - draws one gauge screen from a G3Packet. Pure C++ (host-renderable).
#pragma once
#include "gfx.h"
#include "g3_packet.h"

#define UI_HIST_N 120     // history samples (1 per second = 2 minutes)

struct UiLocal {          // display-side state that is NOT in the packet
  int role = 1;           // 1 = EGT, 2 = BOOST/EBP, 3 = UTILITY
  bool link_ok = false;
  uint32_t now_ms = 0;
  bool imu_ok = false;
  float pitch = 0, roll = 0;   // degrees, already zeroed
  float hist[UI_HIST_N];
  int hist_n = 0, hist_head = 0;
  uint32_t toast_until = 0;
  char toast[24] = {0};
  float link_quality = 100;    // % packets received
};

void ui_render(Canvas& cv, const G3Packet& p, const UiLocal& L);
void ui_hist_push(UiLocal& L, float v);
float ui_hist_value(int role, const G3Packet& p, bool* valid);   // what the history graph tracks

// Demo / preview scenarios (used by host preview renderer and display "demo" command)
int  ui_demo_count();
const char* ui_demo_name(int i);
int  ui_demo_role(int i);
void ui_demo_build(int i, G3Packet& p, UiLocal& L);
