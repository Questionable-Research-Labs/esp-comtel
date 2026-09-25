// ============================================================================
//  button_pins.h -- reserved ESP32 GPIOs for plugin button input
//
//  Single source of truth for which pins a plugin may use for buttons.
//  Wiring: docs/wiring.md (Step 5).
//
//  DECLARATION ONLY. This header defines names, nothing else -- it calls no
//  pinMode(), reads no pins, configures no pull-ups. Each plugin owns its own
//  input handling (pinMode + digitalRead + debounce). Including this header
//  compiles to nothing; it just tells a plugin which pins are safe to claim.
//
//  These pins are DISJOINT from the ComtelPanel driver's sign-signal GPIOs
//  (4, 5, 13, 14, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27 -- see
//  comtel_panel.h). A plugin that uses only the pins below cannot collide
//  with the display.
// ============================================================================
#pragma once

// --- Primary buttons --------------------------------------------------------
// The two physically-wired push buttons (docs/wiring.md). Both support the
// ESP32 internal pull-up, so a plugin uses INPUT_PULLUP and no resistor:
// wire the button between the pin and GND.
static const int BTN_PIN_1 = 32;   // e.g. next message / left
static const int BTN_PIN_2 = 33;   // e.g. speed / right

// --- Additional available pins (for plugins needing >2 buttons) -------------
// GPIO 2 is a strapping/boot pin (drives the onboard LED on many devkits and
// must not be held low at reset by an external source). It works with
// INPUT_PULLUP once booted -- tetris uses it as a third button -- but prefer
// the primary pair first.
static const int BTN_PIN_EXTRA = 2;   // strapping/boot caveat; internal pull-up OK

// GPIO 15 is also a strapping/boot pin (must not be held low at reset -- pulls
// the chip into a debug/silent-boot mode). Like GPIO 2 it works with
// INPUT_PULLUP once booted, so it is a valid internal-pull-up button pin --
// prefer the primary pair (32/33) and BTN_PIN_EXTRA (2) before reaching for it.
static const int BTN_PIN_EXTRA_2 = 15; // strapping/boot caveat; internal pull-up OK

// Input-only pins: usable as button inputs but they have NO internal pull-up
// (or pull-down). A plugin using any of these MUST add an external pull-up
// resistor; INPUT_PULLUP has no effect here.
static const int BTN_PIN_INPUT_ONLY_1 = 34;  // no internal pull-up -- needs external resistor
static const int BTN_PIN_INPUT_ONLY_2 = 35;  // no internal pull-up -- needs external resistor
static const int BTN_PIN_INPUT_ONLY_3 = 36;  // no internal pull-up -- needs external resistor (VP)
static const int BTN_PIN_INPUT_ONLY_4 = 39;  // no internal pull-up -- needs external resistor (VN)
