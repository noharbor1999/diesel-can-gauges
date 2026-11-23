// knob.h - EC11 rotary encoder + push switch, debounced for a bouncing diesel cab.
#pragma once
#include <stdint.h>

#define KNOB_STEPS_PER_DETENT 4     // most EC11 = 4 quadrature edges per detent; set 2 if it takes TWO clicks to move one step
#define KNOB_DEBOUNCE_MS      30    // switch must be stable this long
#define KNOB_MIN_PRESS_MS     40    // shorter presses are ignored (vibration)
#define KNOB_HOLD_MS          1500  // hold time for "reset peak"
#define KNOB_TURN_LOCKOUT_MS  120   // ignore rotation right after the switch moves

struct KnobEvents { int turn; bool click; bool hold; };
void knob_begin(int pin_a, int pin_b, int pin_sw);
KnobEvents knob_poll();            // call often (every loop)
