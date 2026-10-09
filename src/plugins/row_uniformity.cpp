// @plugin: Row-brightness uniformity test card
// ============================================================================
//  row_uniformity -- test cards for the bright-row investigation, plus a live
//  A/B of the driver's burst calibration. Physical rows are counted from 0 at
//  the BOTTOM (canvas y = 15 - physical row).
//
//  BTN 1 (GPIO 32) cycles the test card:
//    0  full-screen white   -- the uniformity field: every row should match
//    1  physical rows 0+8   -- pair 0 alone, the rows the bug over-lights
//    2  staircase           -- one 6-px dash per row, red = lower half
//                              (rows 0-7), green = upper half (rows 8-15);
//                              a mis-latched pair shows a dash in the wrong
//                              half or leaves a row blank
//    3  rows 0+8 vs 1+9     -- side-by-side comparison pairs: left half lights
//                              rows 0/8, right half rows 1/9
//
//  BTN 2 (GPIO 33) cycles the burst calibration, so before/after can be
//  compared without reflashing:
//    0  fixed     scanDuringBurst = true,  burstClocks = 1152  (defaults)
//    1  baseline  scanDuringBurst = false, burstClocks = 4608  (pre-fix)
//    2  short     scanDuringBurst = false, burstClocks = 1152  (burst cut only)
//    3  minimal   scanDuringBurst = true,  burstClocks = 576   (one payload)
//
//  Serial reports the active card, the active calibration and the measured
//  mean show() duration, so task 1.3 / 2.4 / 4.1 numbers can be read off the
//  monitor. Single keys over serial do what the buttons do, for driving the
//  test card from a host: 'c' next card, 'b' next calibration, '0'-'3' pick a
//  calibration directly, '?' reprint the state.
//  The calibration it sets is undone by the host when switching away.
//  Depends only on the ComtelPanel driver and the host interface.
// ============================================================================
#include <Arduino.h>
#include "../host/plugin.h"
#include "../driver/button_pins.h"

namespace {

ComtelPanel *hostPanel = nullptr;   // the host's panel, set by start()

// canvas y for a physical row counted from 0 at the bottom
static inline int rowY(int physicalRow) { return 15 - physicalRow; }

static const int  N_CARDS = 4;
static const int  N_CFGS  = 4;
static const char *CARD_NAME[N_CARDS] = {"white", "rows 0+8", "staircase",
                                         "0+8 vs 1+9"};
static const char *CFG_NAME[N_CFGS]  = {"fixed", "baseline (pre-fix)",
                                        "short burst only", "minimal burst"};

struct Cfg { bool scan; uint32_t clocks; };
static const Cfg CFG[N_CFGS] = {
  {true,  1152},   // driver defaults
  {false, 4608},   // pre-2026-07-26 behaviour: ADDR parked, 8 payloads
  {false, 1152},   // isolates the shorter burst from the in-burst scan
  {true,   576},   // one payload per burst
};

static int card = 0, cfg = 0;

// show() timing, accumulated between serial reports
static uint32_t showUsSum = 0;
static uint32_t showCount = 0;
static uint32_t lastReport = 0;

static void draw() {
  GFXcanvas8 &c = hostPanel->canvas;
  c.fillScreen(COMTEL_OFF);
  switch (card) {
    case 0:
      c.fillScreen(COMTEL_WHITE);
      break;
    case 1:
      c.drawFastHLine(0, rowY(0), ComtelPanel::W, COMTEL_WHITE);
      c.drawFastHLine(0, rowY(8), ComtelPanel::W, COMTEL_WHITE);
      break;
    case 2:
      // One dash per physical row, marching left to right as rows go up.
      for (int r = 0; r < 16; r++) {
        c.fillRect(r * 6, rowY(r), 6, 1, r < 8 ? COMTEL_RED : COMTEL_GREEN);
      }
      break;
    case 3:
      // Pair 0 on the left, pair 1 on the right: any brightness difference
      // between the two halves is the duty imbalance, seen side by side.
      c.fillRect(0, rowY(0), 48, 1, COMTEL_WHITE);
      c.fillRect(0, rowY(8), 48, 1, COMTEL_WHITE);
      c.fillRect(48, rowY(1), 48, 1, COMTEL_WHITE);
      c.fillRect(48, rowY(9), 48, 1, COMTEL_WHITE);
      break;
  }
}

static void applyCfg() {
  ComtelPanel &panel = *hostPanel;
  panel.scanDuringBurst = CFG[cfg].scan;
  panel.burstClocks     = CFG[cfg].clocks;
  showUsSum = showCount = 0;                  // restart the timing average
  Serial.printf("card=%s  calibration=%s (scanDuringBurst=%d, burstClocks=%u)\n",
                CARD_NAME[card], CFG_NAME[cfg],
                (int)panel.scanDuringBurst, (unsigned)panel.burstClocks);
}

// Live level with a lockout; pressed = LOW (button wires pin -> GND).
static bool pressed(int pin, uint32_t &lockUntil) {
  if (millis() < lockUntil) return false;
  if (digitalRead(pin) != LOW) return false;
  lockUntil = millis() + 250;
  return true;
}

static uint32_t lock1 = 0, lock2 = 0;    // button lockouts

void start(ComtelPanel &panel) {
  hostPanel = &panel;
  card = cfg = 0;
  lock1 = lock2 = 0;
  pinMode(BTN_PIN_1, INPUT_PULLUP);
  pinMode(BTN_PIN_2, INPUT_PULLUP);
  draw();
  applyCfg();
  lastReport = millis();
}

// Same actions as the buttons, driven from a serial monitor or a host script.
static void handleSerial() {
  while (Serial.available()) {
    int ch = Serial.read();
    if (ch == 'c') { card = (card + 1) % N_CARDS; draw(); applyCfg(); }
    else if (ch == 'b') { cfg = (cfg + 1) % N_CFGS; applyCfg(); }
    else if (ch >= '0' && ch < '0' + N_CFGS) { cfg = ch - '0'; applyCfg(); }
    else if (ch == '?') applyCfg();
  }
}

void tick(ComtelPanel &panel) {
  handleSerial();
  if (pressed(BTN_PIN_1, lock1)) { card = (card + 1) % N_CARDS; draw(); applyCfg(); }
  if (pressed(BTN_PIN_2, lock2)) { cfg  = (cfg  + 1) % N_CFGS;  applyCfg(); }

  // Push the same frame continuously: the duty imbalance only exists while
  // bursts are running, so a static card must still be re-sent every loop.
  uint32_t t0 = micros();
  panel.show();
  showUsSum += micros() - t0;
  showCount++;

  if (millis() - lastReport >= 2000) {
    lastReport = millis();
    if (showCount) {
      uint32_t mean = showUsSum / showCount;
      Serial.printf("%s / %s: show() mean %u us over %u frames (~%u fps)\n",
                    CARD_NAME[card], CFG_NAME[cfg], (unsigned)mean,
                    (unsigned)showCount, (unsigned)(1000000UL / (mean ? mean : 1)));
      showUsSum = showCount = 0;
    }
  }
}

}  // namespace

COMTEL_PLUGIN(row_uniformity, "Row-brightness uniformity test card",
              start, tick, nullptr);
