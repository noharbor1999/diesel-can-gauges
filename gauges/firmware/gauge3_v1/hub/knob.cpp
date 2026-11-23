#include "knob.h"
#include <Arduino.h>

static int pa, pb, psw;
static volatile int32_t q_steps = 0;
static volatile uint8_t q_state = 0;
// valid quadrature transitions: +1 / -1, invalid (bounce) = 0
static const int8_t QDEC[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

static void IRAM_ATTR enc_isr() {
  uint8_t ab = (digitalRead(pa) << 1) | digitalRead(pb);
  q_state = ((q_state << 2) | ab) & 0x0F;
  q_steps += QDEC[q_state];
}

void knob_begin(int a, int b, int sw) {
  pa = a; pb = b; psw = sw;
  pinMode(pa, INPUT_PULLUP); pinMode(pb, INPUT_PULLUP); pinMode(psw, INPUT_PULLUP);
  q_state = (digitalRead(pa) << 1) | digitalRead(pb);
  attachInterrupt(digitalPinToInterrupt(pa), enc_isr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pb), enc_isr, CHANGE);
}

KnobEvents knob_poll() {
  KnobEvents e = {0, false, false};
  static bool stable_pressed = false, raw_last = false, hold_fired = false;
  static uint32_t raw_change = 0, press_ms = 0, sw_move_ms = 0;
  static int32_t acc = 0;
  uint32_t now = millis();

  // ---- switch (active low) ----
  bool raw = digitalRead(psw) == LOW;
  if (raw != raw_last) { raw_last = raw; raw_change = now; }
  if (raw != stable_pressed && now - raw_change >= KNOB_DEBOUNCE_MS) {
    stable_pressed = raw; sw_move_ms = now;
    if (stable_pressed) { press_ms = now; hold_fired = false; }
    else {
      uint32_t d = now - press_ms;
      if (!hold_fired && d >= KNOB_MIN_PRESS_MS) e.click = true;
    }
  }
  if (stable_pressed && !hold_fired && now - press_ms >= KNOB_HOLD_MS) { hold_fired = true; e.hold = true; }

  // ---- rotation ----
  noInterrupts(); int32_t s = q_steps; q_steps = 0; interrupts();
  if (stable_pressed || now - sw_move_ms < KNOB_TURN_LOCKOUT_MS) { acc = 0; return e; }
  acc += s;
  while (acc >= KNOB_STEPS_PER_DETENT)  { e.turn++; acc -= KNOB_STEPS_PER_DETENT; }
  while (acc <= -KNOB_STEPS_PER_DETENT) { e.turn--; acc += KNOB_STEPS_PER_DETENT; }
  return e;
}
