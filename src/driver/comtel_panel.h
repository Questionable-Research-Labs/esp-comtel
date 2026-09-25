// ============================================================================
//  ComtelPanel -- driver for the Comtel 96x16 RGB LED sign, ESP32 direct-drive
//  Protocol: docs/PROTOCOL.md   Wiring: docs/wiring.md
//
//  Usage:
//    ComtelPanel panel;
//    panel.begin();
//    ... draw on panel.canvas (Adafruit GFXcanvas8, 96x16) using COMTEL_* ...
//    panel.show();                  // push canvas to the glass (~33 ms)
//    loop: panel.service();         // keep the display multiplexed
//
//  DUTY: show() and service() between them hand every scan address an equal
//  slice of time, so all 16 rows are equally bright. That is not automatic --
//  a write burst freezes the display on whichever address is presented, so the
//  burst multiplexes as it shifts (scanDuringBurst) and is no longer than it
//  needs to be (burstClocks). See the calibration block below; a residual
//  ~2% favours rows 0 and 8, the pre-latch ADDR = 0 settle window.
//
//  COLOR: the panel drives separate R, G and B subpixels per dot. The canvas is
//  one byte per pixel; only the low 3 bits matter -- an RGB on/off bitmask
//  (bit 0 = red, bit 1 = green, bit 2 = blue). Each active channel lights at
//  full brightness; there are no gradients. Draw with the named COMTEL_*
//  constants below via any Adafruit GFX call, e.g.
//    panel.canvas.fillScreen(COMTEL_OFF);
//    panel.canvas.drawPixel(x, y, COMTEL_CYAN);
//  Passing a bare 1 (the old 1bpp "on") now renders RED only -- use
//  COMTEL_WHITE (7) for the previous all-channels-on white.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>

// 3-bit RGB on/off palette for panel.canvas (bit0=R, bit1=G, bit2=B).
static const uint8_t COMTEL_OFF     = 0;  // 000  none
static const uint8_t COMTEL_RED     = 1;  // 001  R
static const uint8_t COMTEL_GREEN   = 2;  // 010  G
static const uint8_t COMTEL_YELLOW  = 3;  // 011  R+G
static const uint8_t COMTEL_BLUE    = 4;  // 100  B
static const uint8_t COMTEL_MAGENTA = 5;  // 101  R+B
static const uint8_t COMTEL_CYAN    = 6;  // 110  G+B
static const uint8_t COMTEL_WHITE   = 7;  // 111  R+G+B

class ComtelPanel {
 public:
  static const int W = 96;
  static const int H = 16;

  GFXcanvas8 canvas;         // 8bpp; low 3 bits = RGB on/off (see COMTEL_*)

  ComtelPanel() : canvas(W, H) {}

  void begin();
  void show();               // write the whole canvas (8 pair bursts, ~12 ms)
  void service();            // one display-scan pass (~1.6 ms); call often

  // calibration (defaults = verified working values)
  int      rowShiftLo   = 0;    // lower-half row-decoder offset (PROTOCOL.md);
                                // 0 = straight, staircase-verified 2026-07-15
  uint32_t clockKHz     = 2000; // burst data clock
  int      rowHoldUs    = 205;  // display dwell per scan address
  uint32_t interBurstMs = 2;    // display keep-alive between pair bursts

  // Burst length in data clocks. The pair payload is 576 clocks; the burst
  // repeats it, so this is rounded DOWN to a whole number of payloads and
  // clamped to one (a shorter burst cannot fill the shift register). The
  // original sign sent 8 copies (4608); 2 keeps a full spare copy of margin
  // while cutting the per-burst display freeze from ~2.3 ms to ~0.6 ms.
  uint32_t burstClocks  = 1152;

  // Keep multiplexing the display *through* the write burst. The shift itself
  // is scan-address-independent -- only the U12 falling edge consults ADDR --
  // so the burst hands each of the 8 addresses an equal slice of its clocks
  // and restores ADDR = 0 before latching. With this off, ADDR stays parked at
  // 0 for the whole burst and pair 0 (physical rows 0 and 8) is visibly
  // brighter than every other row. Off + burstClocks = 4608 is the
  // pre-2026-07-26 behaviour, kept as the A/B reference and rollback.
  bool     scanDuringBurst = true;

  // How long ADDR = 0 is held, settled, before the U12 falling edge that
  // latches a pair. The latch destination is sampled at that edge and tracks
  // ADDR (PROTOCOL.md); this is the margin that makes it unambiguous.
  uint32_t latchSettleUs = 10;

 private:
  // ESP32 GPIO -> 74HCT245 -> ribbon (docs/wiring.md). All pins must be <32.
  static const int PIN_CLK = 23;                    // ribbon 10
  static const int PIN_A = 25, PIN_B = 26, PIN_C = 27; // ribbon 18,19,20
  static const int PIN_U12 = 13;                    // ribbon 12
  static const int PIN_W0 = 4, PIN_W1 = 5, PIN_W2 = 14; // ribbon 14,15,16
  static const int DATA_PIN[6];                     // ribbon 1,2,4,6,7,8

  static const int PAIR_CLOCKS = 576;    // one row-pair's payload; burst minimum

  uint32_t cpuMHz = 240;
  uint32_t clkM = 0, u12M = 0, aM = 0, bM = 0, cM = 0;
  uint32_t w0M = 0, w1M = 0, w2M = 0;
  uint32_t dM[6] = {0}, allDataM = 0;
  uint32_t pairSet[PAIR_CLOCKS], pairClr[PAIR_CLOCKS];
  uint32_t addrSet[8] = {0}, addrClr[8] = {0}; // per-scan-address ADDR masks
  uint8_t  scanAddr = 0;   // rolling display scan address: service() and the
                           // burst both advance it, so exposure stays even

  void setAddr(uint8_t a);
  void setWriteAddr(uint8_t v);
  void buildPair(int p);
  void IRAM_ATTR burstPair(uint32_t halfCyc);
};
