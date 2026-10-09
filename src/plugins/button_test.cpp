// @plugin: Button tester -- 6 buttons, one square each
// ============================================================================
//  button_test -- a diagnostic plugin that maps each of six control buttons to
//  a distinct 14x14 square on the 96x16 glass. While a button is held its
//  square is drawn; when released the square disappears. Multiple held buttons
//  show at once (live level, not latched). Each press/release edge is also
//  logged over serial so wiring can be checked from the monitor.
//
//  Pins come from the button_pins.h source of truth. 32/33/2/15 use the ESP32
//  internal pull-up (button wired pin->GND); 34/35 are input-only and rely on
//  external 10 kOhm pull-ups to 3.3 V (see docs/wiring.md Step 5).
//  Depends only on the ComtelPanel driver and the host interface.
// ============================================================================
#include <Arduino.h>
#include "../host/plugin.h"
#include "../driver/button_pins.h"

namespace {

// One row drives both the pin list and the on-glass layout, so the button->
// square mapping lives in a single place. Squares are laid out left->right in
// 16-px slots (x = index*16), 14x14 inset by 1 px, giving a 2-px gutter between
// squares and a 1-px top/bottom margin on the 16-px-tall panel.
struct Button {
  int     pin;        // ESP32 GPIO
  uint8_t colour;     // COMTEL_* palette value
  int     x;          // left edge of this button's 16-px slot
  bool    internalPU; // true -> INPUT_PULLUP; false -> INPUT (external pull-up)
};

static Button buttons[6] = {
  {BTN_PIN_1,           COMTEL_RED,     0 * 16, true},
  {BTN_PIN_2,           COMTEL_GREEN,   1 * 16, true},
  {BTN_PIN_EXTRA_2,     COMTEL_YELLOW,  2 * 16, true},   // GPIO 15
  {BTN_PIN_EXTRA,       COMTEL_BLUE,    3 * 16, true},   // GPIO 2
  {BTN_PIN_INPUT_ONLY_1, COMTEL_MAGENTA, 4 * 16, false}, // GPIO 34, external PU
  {BTN_PIN_INPUT_ONLY_2, COMTEL_CYAN,    5 * 16, false}, // GPIO 35, external PU
};

static const int N = sizeof(buttons) / sizeof(buttons[0]);

// Last-frame pressed state, for detecting press/release edges to log.
static bool prevPressed[6] = {false, false, false, false, false, false};

void start(ComtelPanel &) {
  for (int i = 0; i < N; i++) prevPressed[i] = false;

  // Internal-pull-up-capable pins use INPUT_PULLUP; input-only pins (34/35)
  // have no internal pull-up, so they are plain INPUT and depend on external
  // resistors for their idle-high level.
  for (int i = 0; i < N; i++) {
    pinMode(buttons[i].pin, buttons[i].internalPU ? INPUT_PULLUP : INPUT);
  }
}

void tick(ComtelPanel &panel) {
  GFXcanvas8 &c = panel.canvas;
  c.fillScreen(COMTEL_OFF);

  // Read all six live each frame. Buttons wire pin->GND, so LOW = pressed.
  // Draw every pressed button's square in the same frame (simultaneous holds
  // show together); released buttons simply are not drawn, clearing them.
  for (int i = 0; i < N; i++) {
    bool pressed = digitalRead(buttons[i].pin) == LOW;

    if (pressed) {
      c.fillRect(buttons[i].x + 1, 1, 14, 14, buttons[i].colour);
    }

    // One line per edge so wiring can be verified over the serial monitor.
    if (pressed != prevPressed[i]) {
      Serial.printf("BTN%d (GPIO%d) %s\n",
                    i + 1, buttons[i].pin, pressed ? "pressed" : "released");
      prevPressed[i] = pressed;
    }
  }

  panel.show();
}

}  // namespace

COMTEL_PLUGIN(button_test, "Button tester -- 6 buttons, one square each",
              start, tick, nullptr);
