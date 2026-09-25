// ============================================================================
//  Comtel sign DIAGNOSTICS   (build env: diag)
//  The complete reverse-engineering firmware, frozen as of the night the
//  protocol was cracked (2026-07-08; see docs/PROTOCOL.md). Contains every
//  probe built along the way: solid/per-clock patterns, one-shot bursts,
//  write-bus experiments, sync-hunt pulsing, sweeps, test cards, and the
//  working scroll demo. For normal use, build env `drive` instead.
//  ----------------------------------------------------------------------------
//  (historical header follows -- claims below are partly superseded)
//  Comtel sign driver -- v3: gap-free timed burst
//  ----------------------------------------------------------------------------
//  Decoded protocol (via scope):
//   * 6-bit grayscale, frame-buffered driver. 96x16 RGB, dual-half 1/8 scan.
//   * FRAME UPDATE ("burst"): park ADDR=0, raise U12, shift the whole frame as
//     ONE CONTINUOUS ~4608-clock train at 5 MHz (data sampled on the RISING
//     edge; clock idles HIGH), then drop U12 -> chip latches the frame.
//   * DISPLAY: clock idle HIGH, U12 low; scan ADDR 0..7 (~205us each) and the
//     chip outputs each addressed row from its internal buffer. Scan forever.
//   * For ON/OFF we set each subpixel's 6 grayscale bits all-1 / all-0.
//
//  Pins (ESP32 -> 74HCT245 -> ribbon):
//   CLK=23->r10  DATA D0..5=16,17,18,19,21,22->r1,2,4,6,7,8
//   ADDR A,B,C=25,26,27->r18,19,20   U12=13->r12 (spare 245 ch)
//   ribbon14,15,16 -> tied to GND (held low in hardware)
//
//  v3 (2026-07-03) -- see docs/AUDIT-2026-07-03.md. The old shift loop was
//  nop-timed and never masked interrupts, so the FreeRTOS 1 kHz tick punched
//  us-scale clock gaps into every burst: the scope-confirmed waveform (one
//  gap-free 4608-clock 5 MHz train) was never actually reproduced. The burst
//  engine now runs from IRAM with interrupts disabled and schedules every edge
//  off the CPU cycle counter. Before trusting panel results, check `s`: the
//  measured burst must be ~921.6 us at 5 MHz (and scope-verify continuity).
//
//  v3.1: the CPU-timed loop tops out at ~3.33 MHz (a GPIO write + deadline
//  poll costs ~72 cycles/period), and a gap-free 3.33 MHz burst still only
//  wrote row 0. For presets >=4 MHz the clock now comes from the LEDC PWM
//  peripheral instead: hardware-exact frequency, gated on/off by the cycle
//  counter (+-1 clock -- harmless for solid fills since the data lines are
//  constant and extra clocks shift identical bits). Interrupts stay masked
//  across the burst so ADDR/U12/gap timing stays deterministic.
//  Also learned (holdAddr=0 test): UNWRITTEN rows light autonomously, ignoring
//  ADDR -- the lit/dark panel is a live "which rows got written" map.
//
//  Interactive over serial @115200:
//   patterns: 0=off 1=all-on  a..f=only data line 0..5 (identify colour/half)
//   load:     [ ]=clock freq preset (250k..8MHz, default 5MHz)
//             , . =refreshMs(/,x2)  m=auto-refresh on/off
//             v=U12 polarity  w=scan ADDR during shift  p=U12 latch-per-row
//   display:  - + =rowHold(us)  g=cycle holdAddr (freeze on 1 addr; -1=scan)
//   diagnostics (write-pointer characterization):
//             q=one-shot 1-row load (576 clks + latch, auto-refresh off)
//             Q=one-shot 2-row load (1152 clks + latch)
//             j=cycle gap injection 0/10/50 us at mid-burst (clock 2304)
//   X=strategy SWEEP (13 combos; n/N step)   s=state+burst timing  ?=help
// ============================================================================

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <soc/gpio_reg.h>

#define PIN_CLK 23
static const int DATA_GPIO[6] = {16, 17, 18, 19, 21, 22}; // r1,2,4,6,7,8
#define PIN_A 25
#define PIN_B 26
#define PIN_C 27
#define PIN_U12 13            // shift-enable / latch line -> ribbon 12
// v3.5: ribbon 14/15/16 now DRIVEN (via U2 spare channels) instead of grounded.
// Held LOW at boot (= the old grounded state); sync-hunt pulses them.
#define PIN_R14 4             // -> U2 A4/B4 -> ribbon 14
#define PIN_R15 5             // -> U2 A5/B5 -> ribbon 15
#define PIN_R16 14            // -> U2 A6/B6 -> ribbon 16

#define PANEL_W 96
#define PANEL_H 16
// Lit test patterns draw white (all three channels on). This diag sketch is
// standalone and can't include comtel_panel.h (its #define pin macros clash
// with the driver's class members), so mirror the palette's white value here.
static const uint8_t COMTEL_WHITE = 7;  // 111 = R+G+B (see comtel_panel.h)
#define FRAME_CLOCKS 4608     // 6 planes * 8 addr * 96 col (0.92 ms @ 5 MHz)
#define ROW_CLOCKS   576      // FRAME_CLOCKS / 8

static inline uint32_t ccount() {
  uint32_t c; __asm__ __volatile__("rsr %0,ccount" : "=a"(c)); return c;
}
static inline uint32_t gbit(int g) { return 1UL << g; }

// runtime calibration knobs
static const uint32_t freqPresets[] = {250, 500, 1000, 2000, 2500, 4000, 5000, 8000}; // kHz
static const int NFREQ = sizeof(freqPresets) / sizeof(freqPresets[0]);
static int  freqIdx = 6;        // 5000 kHz -- the measured original rate
static int  rowHoldUs = 205;    // display dwell per address
static bool u12Active = true;   // U12 level during shift (true=HIGH)
static uint32_t refreshMs = 30; // re-load the frame this often to hold it
static bool autoRefresh = true; // periodic reload on/off (off for q/Q tests)
static bool scanDuringShift = false; // advance ADDR 0..7 across the shift?
static int  holdAddr = -1;      // -1 = normal scan; 0..7 = freeze display
static bool latchPerRow = false;// pulse U12 once per row (vs once per frame)?
static uint32_t gapUs = 0;      // inject a clock gap this long at mid-burst

static uint32_t cpuMHz = 240;
static uint32_t clkM, aM, bM, cM, u12M, dM[6], allDataM;
static uint32_t r14M, r15M, r16M;

// sync-hunt knobs: pulse one of r14/15/16 at 576-clock segment boundaries
static int  ctlTarget = 0;          // 'u': 0=none 1=r14 2=r15 3=r16
static bool ctlPulseHigh = true;    // 'i': pulse HIGH (idle low) vs LOW (idle high)
static uint32_t ctlPulseUs = 2;     // 'k': 1/2/5/10 us
static bool ctlEveryBoundary = true;// 'o': every 576-boundary vs frame-start only
static bool ctlSegAddr = false;     // 'r': drive {r14,r15,r16} = segment number
                                    // (write-address-bus hypothesis; overrides pulses)
static const char *ctlNames[] = {"none", "r14", "r15", "r16"};

static int staticBus = -1;          // 'R': -1=off, 0..7 held on {r14,r15,r16}
                                    // for the entire frame (r14=LSB)

static uint32_t ctlMaskOf(int t) {
  return (t == 1) ? r14M : (t == 2) ? r15M : (t == 3) ? r16M : 0;
}
// park all ctl lines at idle: LOW, except a pulse-LOW target idles HIGH,
// or the static bus value if set
static void applyCtlIdle() {
  if (staticBus >= 0) {
    uint32_t s = 0, c = 0;
    (staticBus & 1) ? (s |= r14M) : (c |= r14M);
    (staticBus & 2) ? (s |= r15M) : (c |= r15M);
    (staticBus & 4) ? (s |= r16M) : (c |= r16M);
    REG_WRITE(GPIO_OUT_W1TS_REG, s);
    REG_WRITE(GPIO_OUT_W1TC_REG, c);
    return;
  }
  REG_WRITE(GPIO_OUT_W1TC_REG, r14M | r15M | r16M);
  if (ctlTarget && !ctlPulseHigh)
    REG_WRITE(GPIO_OUT_W1TS_REG, ctlMaskOf(ctlTarget));
}

// last burst measurement (to verify the clock without a scope)
static uint32_t lastBurstCyc = 0, lastBurstClocks = 0;

// current solid pattern: bit i set => data line i driven high during shift
static uint8_t solidMask = 0;   // 0 = all off
static char patName[24] = "off";

static uint32_t halfCycFor(uint32_t kHz) {
  uint32_t h = (cpuMHz * 1000UL) / (2UL * kHz);
  return (h < 12) ? 12 : h;     // floor = 10 MHz @ 240 MHz CPU
}

static inline void setAddr(uint8_t a) {
  uint32_t s = 0, c = 0;
  (a & 1) ? (s |= aM) : (c |= aM);
  (a & 2) ? (s |= bM) : (c |= bM);
  (a & 4) ? (s |= cM) : (c |= cM);
  REG_WRITE(GPIO_OUT_W1TS_REG, s);
  REG_WRITE(GPIO_OUT_W1TC_REG, c);
}
static inline void u12Level(bool high) {
  REG_WRITE(high ? GPIO_OUT_W1TS_REG : GPIO_OUT_W1TC_REG, u12M);
}
static inline void setDataConst(uint8_t mask) {
  uint32_t setm = 0, clrm = 0;
  for (int i = 0; i < 6; i++) ((mask >> i) & 1) ? (setm |= dM[i]) : (clrm |= dM[i]);
  REG_WRITE(GPIO_OUT_W1TS_REG, setm);
  REG_WRITE(GPIO_OUT_W1TC_REG, clrm);
}
static inline void spinUntil(uint32_t t) {
  while ((int32_t)(ccount() - t) < 0) {}
}

// ---------------------------------------------------------------------------
// Burst engine. Data lines must already be set by the caller; ADDR must be
// parked by the caller unless scanAddr (then the engine drives it 0..7 at each
// ROW_CLOCKS boundary). Runs from IRAM with interrupts masked so the clock
// train has NO gaps; every edge is scheduled off the cycle counter (deadline-
// based, so jitter doesn't accumulate). If the loop can't keep up (too-high
// preset) the frequency degrades but stays continuous -- check `s`.
// gapCycles>0 parks the clock at idle-HIGH that long before clock #gapAtClk.
// Returns elapsed CPU cycles (incl. U12 setup/latch holds of 4 half-periods).
// ---------------------------------------------------------------------------
static uint32_t IRAM_ATTR burstEngine(int clocks, uint32_t halfCyc,
                                      bool scanAddr, bool perRowLatch,
                                      bool u12hi, int gapAtClk,
                                      uint32_t gapCycles) {
  noInterrupts();
  uint32_t start = ccount();
  if (perRowLatch) {
    int rows = (clocks + ROW_CLOCKS - 1) / ROW_CLOCKS;
    for (int row = 0; row < rows; row++) {
      if (scanAddr) setAddr(row);
      u12Level(u12hi);
      uint32_t t = ccount() + 4 * halfCyc;    // U12-to-first-clock setup
      spinUntil(t);
      int n = clocks - row * ROW_CLOCKS;
      if (n > ROW_CLOCKS) n = ROW_CLOCKS;
      for (int k = 0; k < n; k++) {
        REG_WRITE(GPIO_OUT_W1TC_REG, clkM);
        t += halfCyc; spinUntil(t);
        REG_WRITE(GPIO_OUT_W1TS_REG, clkM);
        t += halfCyc; spinUntil(t);
      }
      t += 4 * halfCyc; spinUntil(t);         // hold before latch
      u12Level(!u12hi);                       // latch this row
      t += 4 * halfCyc; spinUntil(t);         // latch-low dwell
    }
  } else {
    if (scanAddr) setAddr(0);
    u12Level(u12hi);
    uint32_t t = ccount() + 4 * halfCyc;
    spinUntil(t);
    int rowNext = ROW_CLOCKS, row = 1;
    for (int k = 0; k < clocks; k++) {
      if (gapCycles && k == gapAtClk) { t += gapCycles; spinUntil(t); }
      if (scanAddr && k == rowNext) { setAddr(row++); rowNext += ROW_CLOCKS; }
      REG_WRITE(GPIO_OUT_W1TC_REG, clkM);
      t += halfCyc; spinUntil(t);
      REG_WRITE(GPIO_OUT_W1TS_REG, clkM);
      t += halfCyc; spinUntil(t);
    }
    t += 4 * halfCyc; spinUntil(t);
    u12Level(!u12hi);                         // latch the frame
  }
  uint32_t elapsed = ccount() - start;
  interrupts();
  return elapsed;
}

// ---------------------------------------------------------------------------
// PWM-clocked burst. For rates the CPU loop can't sustain (>= ~3.3 MHz) the
// clock comes from LEDC: hardware-exact frequency, no jitter, no gaps. The
// train is gated by attaching/detaching the pin to the free-running PWM,
// bounded by cycle-counter deadlines (+-1 clock; harmless for constant data).
// Detaching snaps the pin back to its GPIO level (idle HIGH). ADDR changes
// (scanAddr) happen on the fly at ROW_CLOCKS boundaries while the clock runs.
// gapCycles>0 parks the clock at idle-HIGH mid-train before clock #gapAtClk
// (gap path ignores scanAddr -- gap experiments run with ADDR parked).
// ---------------------------------------------------------------------------
#define LEDC_CH 0
static inline void clkPwmOn()  { ledcAttachPin(PIN_CLK, LEDC_CH); }
static inline void clkPwmOff() {
  ledcDetachPin(PIN_CLK);
  REG_WRITE(GPIO_OUT_W1TS_REG, clkM);          // park at idle HIGH
}

static uint32_t burstPWM(int clocks, uint32_t kHz, bool scanAddr,
                         bool perRowLatch, bool u12hi,
                         int gapAtClk, uint32_t gapCycles) {
  const uint32_t cpc = (cpuMHz * 1000UL) / kHz;   // CPU cycles per clock period
  ledcSetup(LEDC_CH, kHz * 1000UL, 1);            // 1-bit res: duty 1/2 = 50%
  ledcWrite(LEDC_CH, 1);
  noInterrupts();
  uint32_t start = ccount();
  if (perRowLatch) {
    int rows = (clocks + ROW_CLOCKS - 1) / ROW_CLOCKS;
    for (int row = 0; row < rows; row++) {
      if (scanAddr) setAddr(row);
      u12Level(u12hi);
      spinUntil(ccount() + 2 * cpc);              // U12-to-clock setup
      int n = clocks - row * ROW_CLOCKS;
      if (n > ROW_CLOCKS) n = ROW_CLOCKS;
      clkPwmOn();
      spinUntil(ccount() + (uint32_t)n * cpc);
      clkPwmOff();
      spinUntil(ccount() + 2 * cpc);              // hold before latch
      u12Level(!u12hi);                           // latch this row
      spinUntil(ccount() + 2 * cpc);              // latch-low dwell
    }
  } else {
    if (scanAddr) setAddr(0);
    u12Level(u12hi);
    spinUntil(ccount() + 2 * cpc);
    if (gapCycles && gapAtClk > 0 && gapAtClk < clocks) {
      clkPwmOn();
      spinUntil(ccount() + (uint32_t)gapAtClk * cpc);
      clkPwmOff();                                // gap: clock parked HIGH
      spinUntil(ccount() + gapCycles);
      clkPwmOn();
      spinUntil(ccount() + (uint32_t)(clocks - gapAtClk) * cpc);
      clkPwmOff();
    } else {
      clkPwmOn();
      uint32_t t0 = ccount();
      int done = 0, row = 1;
      while (done < clocks) {
        int next = row * ROW_CLOCKS;
        if (next > clocks) next = clocks;
        spinUntil(t0 + (uint32_t)next * cpc);
        if (scanAddr && next < clocks) setAddr(row);
        done = next; row++;
      }
      clkPwmOff();
    }
    spinUntil(ccount() + 2 * cpc);                // hold before latch
    u12Level(!u12hi);                             // latch the frame
  }
  uint32_t elapsed = ccount() - start;
  interrupts();
  return elapsed;
}

// ---------------------------------------------------------------------------
// v3.2: per-clock data patterns. Solid fills can't distinguish ANY encoding
// (constant in = constant out); these patterns vary the 6 data lines every
// clock so the on-glass shape reveals the clock<->pixel mapping, line
// semantics and grayscale. CPU-timed only (data must sync to the clock), so
// they run at <=2 MHz. patSet/patClr are precomputed W1TS/W1TC masks.
// ---------------------------------------------------------------------------
static uint32_t patSet[FRAME_CLOCKS], patClr[FRAME_CLOCKS];
static int zMode = 0;
static uint8_t lineSel = 0x3F;  // 'y' cycles ALL,0x01..0x20: restricts patterns
                                // to those lines / selects the sync candidate
static const char *zNames[] = {
    "solid",
    "ALT1: 0x3F/0x00 alternating clocks (on lineSel)",
    "GRAD64: value = k & 63 (on lineSel)",
    "ONE: on for clock 0 only (on lineSel)",
    "SEG576: on for first 576 clocks (on lineSel)",
    "LAST1: on for the FINAL clock only (on lineSel)",
    "LAST576: on for the last 576 clocks (on lineSel)",
    "SYNC-HI: 1-clk HIGH pulse on lineSel at each 576 boundary, ALT data on other lines",
    "SYNC-LO: lineSel held HIGH with 1-clk LOW pulse at each 576 boundary, ALT data on other lines",
    "SEGID: segment s carries s+1 lit blocks -- count blocks to identify the segment",
    "MAP1: in last-576 window: subpix j=0 alone, white px at j=288-290, white px at j=573-575",
    "SPARSE: text-like, 12 lit clocks per 576 (white cols 16-17), rest dark",
};
static const int NZ = sizeof(zNames) / sizeof(zNames[0]);

static void buildZ() {
  for (int k = 0; k < FRAME_CLOCKS; k++) {
    uint8_t v = 0;
    bool boundary = (k % ROW_CLOCKS) == 0;
    uint8_t alt = (k & 1) ? 0x00 : 0x3F;
    switch (zMode) {
      case 1: v = alt & lineSel; break;
      case 2: v = (k & 63) & lineSel; break;
      case 3: v = (k == 0) ? lineSel : 0x00; break;
      case 4: v = (k < ROW_CLOCKS) ? lineSel : 0x00; break;
      case 5: v = (k == FRAME_CLOCKS - 1) ? lineSel : 0x00; break;
      case 6: v = (k >= FRAME_CLOCKS - ROW_CLOCKS) ? lineSel : 0x00; break;
      case 7: v = (boundary ? lineSel : 0x00) | (alt & ~lineSel); break;
      case 8: v = (boundary ? 0x00 : lineSel) | (alt & ~lineSel); break;
      case 9: {                       // SEGID: s+1 blocks of 32 clocks, gaps of 32
        int s = k / ROW_CLOCKS, b = (k % ROW_CLOCKS) / 32;
        v = ((b & 1) == 0 && (b / 2) <= s) ? 0x3F : 0x00;
        break;
      }
      case 10: {                      // MAP1: layout probe in the last-576 window
        int j = k - (FRAME_CLOCKS - ROW_CLOCKS);
        v = (j == 0 || (j >= 288 && j < 291) || j >= 573) && j >= 0 ? 0x3F : 0x00;
        break;
      }
      case 11: {                      // SPARSE: 12 lit clocks per segment
        int j = k % ROW_CLOCKS;
        v = (j >= 96 && j < 108) ? 0x3F : 0x00;
        break;
      }
    }
    uint32_t s = 0, c = 0;
    for (int i = 0; i < 6; i++) ((v >> i) & 1) ? (s |= dM[i]) : (c |= dM[i]);
    patSet[k] = s; patClr[k] = c;
  }
}

// Pattern burst: park+single-latch strategy, data updated right after each
// falling edge (sampled on the rising edge). Interrupts off, deadline-timed.
// segGapCyc>0 pauses the clock (parked idle-HIGH, U12 unchanged) that many
// cycles at every 576-clock segment boundary — testing whether the chip
// advances its row-pair write pointer on an inter-segment micro-gap the
// scope's coarse timebase would have hidden. addrStepInGap additionally sets
// ADDR to the segment number during each gap.
static uint32_t IRAM_ATTR burstPattern(int clocks, uint32_t halfCyc, bool u12hi,
                                       uint32_t segGapCyc, bool addrStepInGap,
                                       uint32_t ctlMask, bool ctlHigh,
                                       bool ctlEveryBnd, uint32_t ctlPulseCyc,
                                       bool segAddr) {
  noInterrupts();
  uint32_t start = ccount();
  u12Level(u12hi);
  uint32_t t = ccount() + 4 * halfCyc;
  spinUntil(t);
  for (int k = 0; k < clocks; k++) {
    if ((k % ROW_CLOCKS) == 0 && (k || ctlMask || segAddr)) { // segment boundary
      bool pulse = !segAddr && ctlMask && (ctlEveryBnd || k == 0);
      if (addrStepInGap && k) setAddr((k / ROW_CLOCKS) & 7);
      if (segAddr) {                     // {r14,r15,r16} = segment number
        int seg = (k / ROW_CLOCKS) & 7;
        uint32_t s = 0, c = 0;
        (seg & 1) ? (s |= r14M) : (c |= r14M);
        (seg & 2) ? (s |= r15M) : (c |= r15M);
        (seg & 4) ? (s |= r16M) : (c |= r16M);
        REG_WRITE(GPIO_OUT_W1TS_REG, s);
        REG_WRITE(GPIO_OUT_W1TC_REG, c);
        t += segGapCyc; spinUntil(t);
      } else if (pulse) {                              // sync pulse mid-gap
        t += segGapCyc / 4; spinUntil(t);
        REG_WRITE(ctlHigh ? GPIO_OUT_W1TS_REG : GPIO_OUT_W1TC_REG, ctlMask);
        t += ctlPulseCyc; spinUntil(t);
        REG_WRITE(ctlHigh ? GPIO_OUT_W1TC_REG : GPIO_OUT_W1TS_REG, ctlMask);
        t += segGapCyc / 4; spinUntil(t);
      } else if (k && segGapCyc) {
        t += segGapCyc; spinUntil(t);                // plain gap, clock HIGH
      }
    }
    REG_WRITE(GPIO_OUT_W1TC_REG, clkM);        // falling edge...
    REG_WRITE(GPIO_OUT_W1TS_REG, patSet[k]);   // ...data changes while low
    REG_WRITE(GPIO_OUT_W1TC_REG, patClr[k]);
    t += halfCyc; spinUntil(t);
    REG_WRITE(GPIO_OUT_W1TS_REG, clkM);        // rising edge samples data
    t += halfCyc; spinUntil(t);
  }
  t += 4 * halfCyc; spinUntil(t);
  u12Level(!u12hi);                            // latch
  uint32_t elapsed = ccount() - start;
  interrupts();
  return elapsed;
}

// ---------------------------------------------------------------------------
// v4: THE REAL DRIVER (protocol fully decoded 2026-07-08)
//  - pair p = physical rows p and p+8 (counted from the bottom)
//  - one burst = 576 clocks (only the last 576 matter) + U12 drop = writes ONE pair
//  - per column c (left->right), 6 clocks: B,G,R of lower row, B,G,R of upper row
//  - each data line = one bit of the subpixel's 6-bit brightness (line0=LSB)
//  - bus {r14,r15,r16}: 0 -> every burst writes pair 0 (self-sync);
//    7 -> each burst advances to the next pair (1,2,...,7,0 cycle)
//  - frame update: burst pair 0 at bus=0, then bus=7 and burst pairs 1..7
// ---------------------------------------------------------------------------
static GFXcanvas8 *canvas;                 // 96x16 framebuffer (low 3 bits = RGB)
static uint32_t pairSet[ROW_CLOCKS], pairClr[ROW_CLOCKS];
static bool demoOn = false;
static int  demoX = PANEL_W;
static uint32_t demoT = 0;
static const char *demoMsg = "QUESTIONABLE RESEARCH   ";

static void busSet(int v) {
  uint32_t s = 0, c = 0;
  (v & 1) ? (s |= r14M) : (c |= r14M);
  (v & 2) ? (s |= r15M) : (c |= r15M);
  (v & 4) ? (s |= r16M) : (c |= r16M);
  REG_WRITE(GPIO_OUT_W1TS_REG, s);
  REG_WRITE(GPIO_OUT_W1TC_REG, c);
}

// canvas -> 576-entry set/clr masks for pair p (all 3 channels 0x3F when on)
// ilvMode ('l'): how the column's two 3-clock groups map to the pair's rows:
//  0 = plain  [lower,upper] every column
//  1 = zigzag [lower,upper] on even cols, [upper,lower] on odd cols
//  2 = zigzag [upper,lower] on even cols, [lower,upper] on odd cols
//  3 = block2col (wrong: produced vertical bars)
//  4 = MODULE-SERIAL (CORRECT, verified by module-ID card): 576 clocks = six
//      96-clock chunks, one per 16-col module left->right; within a chunk,
//      16 px of the LOWER row then 16 px of the UPPER row, cols left->right.
static int ilvMode = 4;
static int rowShiftLo = 1;  // 'K': lower-half decoder is off-by-one (addr a ->
                            // physical row a+1 mod 8), compensate in mapping
static void buildPair(int p) {
  for (int j = 0; j < ROW_CLOCKS; j += 3) {  // one subpixel-triplet at a time
    int c; bool lower;
    if (ilvMode == 4) {
      int t = j / 3, q = t & 31;             // triplet, index within module
      lower = (q < 16);
      c = ((t >> 5) << 4) + (q & 15);        // module*16 + col offset
    } else if (ilvMode == 3) {
      int b = j / 6, k = (j % 6) / 3;        // block, pixel-within-block
      lower = ((b & 1) == 0);
      c = (b & ~1) + k;                      // block pair covers cols 2m,2m+1
    } else {
      int g = (j % 6) / 3;
      c = j / 6;
      if (ilvMode == 0)      lower = (g == 0);
      else if (ilvMode == 1) lower = ((c & 1) == 0) ? (g == 0) : (g == 1);
      else                   lower = ((c & 1) == 0) ? (g == 1) : (g == 0);
    }
    int y = lower ? (15 - ((p + rowShiftLo) & 7)) : (7 - p); // canvas y: 0=top
    uint8_t v = canvas->getPixel(c, y) ? 0x3F : 0x00;
    uint32_t s = 0, cl = 0;
    for (int i = 0; i < 6; i++) ((v >> i) & 1) ? (s |= dM[i]) : (cl |= dM[i]);
    pairSet[j] = s;     pairClr[j] = cl;
    pairSet[j + 1] = s; pairClr[j + 1] = cl;
    pairSet[j + 2] = s; pairClr[j + 2] = cl;
  }
}

// clock out one pair's 576 values + latch (interrupts off, deadline-timed)
static uint32_t IRAM_ATTR burstArr(int clocks, uint32_t halfCyc, bool u12hi) {
  noInterrupts();
  uint32_t start = ccount();
  u12Level(u12hi);
  uint32_t t = ccount() + 4 * halfCyc;
  spinUntil(t);
  for (int k = 0, j = 0; k < clocks; k++) {
    REG_WRITE(GPIO_OUT_W1TC_REG, clkM);
    REG_WRITE(GPIO_OUT_W1TS_REG, pairSet[j]);
    REG_WRITE(GPIO_OUT_W1TC_REG, pairClr[j]);
    if (++j == ROW_CLOCKS) j = 0;          // wrap: pair data repeats each 576
    t += halfCyc; spinUntil(t);
    REG_WRITE(GPIO_OUT_W1TS_REG, clkM);
    t += halfCyc; spinUntil(t);
  }
  t += 4 * halfCyc; spinUntil(t);
  u12Level(!u12hi);
  uint32_t elapsed = ccount() - start;
  interrupts();
  return elapsed;
}

static void displayScanOnce();             // fwd

// Write the whole canvas to the panel. Replicates the PROVEN stepping
// conditions exactly: sync once (bus=0 burst -> pair 0, pointer parked), then
// the bus stays at 7 FOREVER and a software phase counter tracks which pair
// each burst hits (cycle 1,2,...,7,0). ADDR parked at 0 before every burst,
// display scan between bursts -- same as the run that painted all 8 pairs.
// Theory (fits all observations): write destination = display ADDR (+offset)
// sampled at burst time; bus value = which pairs are writable (7 = all).
// So: bus=7, park ADDR at (target - 1), burst -> random-access pair writes.
static int addrWriteOff = 7;               // 'l' cycles: setAddr((p+off)&7)
static uint32_t burstGapMs = 2;            // '<'/'>': dwell between bursts
static int pairPhase = -1;                 // kept for demo-start reset only
static void scanFor(uint32_t ms) {         // keep display alive while waiting
  uint32_t until = millis() + ms;
  while ((int32_t)(millis() - until) < 0) displayScanOnce();
}
static void frameOut() {
  uint32_t halfCyc = halfCycFor(2000);
  for (int p = 0; p < 8; p++) {            // direct-address each pair:
    busSet((8 - p) & 7);                   // dest = (8 - bus) mod 8, so
    delayMicroseconds(2);                  // bus = (8 - p) mod 8
    setAddr(0);
    buildPair(p);
    burstArr(FRAME_CLOCKS, halfCyc, u12Active);
    REG_WRITE(GPIO_OUT_W1TC_REG, allDataM);
    scanFor(burstGapMs);
  }
  busSet(0);
}

// The CPU loop sustains ~72 cycles/clock; anything faster gets the PWM clock.
static inline bool usePwm(uint32_t kHz) { return (cpuMHz * 1000UL) / kHz < 80; }

static uint32_t runBurst(int clocks, uint32_t kHz, bool scanAddr,
                         bool perRowLatch, bool u12hi,
                         int gapAtClk, uint32_t gapCycles) {
  if (usePwm(kHz))
    return burstPWM(clocks, kHz, scanAddr, perRowLatch, u12hi, gapAtClk, gapCycles);
  return burstEngine(clocks, halfCycFor(kHz), scanAddr, perRowLatch, u12hi,
                     gapAtClk, gapCycles);
}

// Shift one whole frame of a SOLID pattern (data constant for all clocks).
static void shiftSolid(uint8_t mask) {
  setDataConst(mask);
  uint32_t gapCyc = gapUs * cpuMHz;
  if (!scanDuringShift) setAddr(0);
  lastBurstCyc = runBurst(FRAME_CLOCKS, freqPresets[freqIdx], scanDuringShift,
                          latchPerRow, u12Active, FRAME_CLOCKS / 2, gapCyc);
  lastBurstClocks = FRAME_CLOCKS;
  REG_WRITE(GPIO_OUT_W1TC_REG, allDataM);      // data back low
}

// One-shot partial load: exactly `rows`*576 clocks + a single latch, ADDR
// parked at 0. Kills auto-refresh so the result stays on the chip. Probes the
// write pointer: after a full all-off load, send 1 row of all-on -- which
// address changes? Send another -- does the pointer persist or reset?
static void partialLoad(int rows) {
  autoRefresh = false;
  setDataConst(solidMask);
  setAddr(0);
  lastBurstCyc = runBurst(rows * ROW_CLOCKS, freqPresets[freqIdx], false, false,
                          u12Active, 0, 0);
  lastBurstClocks = rows * ROW_CLOCKS;
  REG_WRITE(GPIO_OUT_W1TC_REG, allDataM);
  Serial.printf("partial load: %d row(s) = %d clocks of '%s' + latch sent.\n"
                "  auto-refresh OFF (m to re-enable). Walk g to see which rows changed.\n",
                rows, rows * ROW_CLOCKS, patName);
}

static void displayScanOnce();   // forward decl

// ---- automatic strategy sweep --------------------------------------------
// mode: 0=park+singleLatch 1=scanAddr+singleLatch 2=park+latchPerRow
//       3=scanAddr+latchPerRow 4=interleaved (shift each row as it's displayed)
struct Combo { uint8_t mode; bool u12hi; uint32_t kHz; const char *name; };
static Combo combos[] = {
    {0, true, 5000,  "0 park+1latch  u12H 5MHz"},
    {0, true, 1000,  "0 park+1latch  u12H 1MHz"},
    {0, false, 5000, "0 park+1latch  u12L 5MHz"},
    {1, true, 5000,  "1 scan+1latch  u12H 5MHz"},
    {1, true, 1000,  "1 scan+1latch  u12H 1MHz"},
    {2, true, 5000,  "2 park+latchPR u12H 5MHz"},
    {2, true, 1000,  "2 park+latchPR u12H 1MHz"},
    {2, false, 5000, "2 park+latchPR u12L 5MHz"},
    {3, true, 5000,  "3 scan+latchPR u12H 5MHz"},
    {3, false, 5000, "3 scan+latchPR u12L 5MHz"},
    {4, true, 5000,  "4 interleaved  u12H 5MHz"},
    {4, true, 1000,  "4 interleaved  u12H 1MHz"},
    {4, false, 5000, "4 interleaved  u12L 5MHz"},
};
static const int NCOMBO = sizeof(combos) / sizeof(combos[0]);
static bool sweepActive = false;
static int  sweepIdx = 0;
static uint32_t blinkT = 0;
static bool blinkOn = false;
static bool needShift = true;

// solid load per a combo (modes 0..3); mode 4 handled in sweepStep
static void shiftFrameMode(uint8_t mask, uint8_t mode, bool u12hi, uint32_t kHz) {
  setDataConst(mask);
  bool scan = (mode == 1 || mode == 3);
  bool perRow = (mode == 2 || mode == 3);
  if (!scan) setAddr(0);
  lastBurstCyc = runBurst(FRAME_CLOCKS, kHz, scan, perRow, u12hi, 0, 0);
  lastBurstClocks = FRAME_CLOCKS;
  REG_WRITE(GPIO_OUT_W1TC_REG, allDataM);
}

static void printCombo() {
  Serial.printf("[sweep %d/%d] %s  <- whole panel blinks? or just 2 rows?\n",
                sweepIdx + 1, NCOMBO, combos[sweepIdx].name);
}

static void sweepStep() {
  Combo c = combos[sweepIdx];
  if (millis() - blinkT >= 700) { blinkT = millis(); blinkOn = !blinkOn; needShift = true; }
  uint8_t mask = blinkOn ? 0x3F : 0x00;
  if (c.mode == 4) {
    // interleaved: shift each row's data right as we display it (no buffer);
    // each engine call raises+drops U12 = one latch per row
    setDataConst(mask);
    for (int row = 0; row < 8; row++) {
      setAddr(row);
      runBurst(ROW_CLOCKS, c.kHz, false, false, c.u12hi, 0, 0);
      delayMicroseconds(rowHoldUs);
    }
    REG_WRITE(GPIO_OUT_W1TC_REG, allDataM);
  } else {
    if (needShift) { shiftFrameMode(mask, c.mode, c.u12hi, c.kHz); needShift = false; }
    displayScanOnce();
  }
}

// One pass of the display multiplex: show each row from the chip's buffer.
static void displayScanOnce() {
  for (uint8_t a = 0; a < 8; a++) {
    setAddr(a);
    delayMicroseconds(rowHoldUs);
  }
}

static uint32_t segGapUs = 0;       // 'h' cycles 0/2/10/50 us
static bool addrStepInGap = false;  // 'H' toggles

static void applyPattern() {
  if (zMode) {
    // per-clock pattern: CPU-timed engine, clamp to 2 MHz
    uint32_t kHz = (freqPresets[freqIdx] > 2500) ? 2000 : freqPresets[freqIdx];
    uint32_t ctlMask = (staticBus >= 0) ? 0 : ctlMaskOf(ctlTarget);
    bool segAddrNow = ctlSegAddr && staticBus < 0;   // static bus overrides
    uint32_t gapCyc = segGapUs * cpuMHz;
    if ((ctlMask || segAddrNow) && !gapCyc) gapCyc = 4 * cpuMHz; // need a window
    setAddr(0);
    lastBurstCyc = burstPattern(FRAME_CLOCKS, halfCycFor(kHz), u12Active,
                                gapCyc, addrStepInGap, ctlMask, ctlPulseHigh,
                                ctlEveryBoundary, ctlPulseUs * cpuMHz,
                                segAddrNow);
    lastBurstClocks = FRAME_CLOCKS;
    REG_WRITE(GPIO_OUT_W1TC_REG, allDataM);
    applyCtlIdle();                       // r14/15/16 back to idle
  } else {
    shiftSolid(solidMask);
  }
}

static void printState() {
  uint32_t kHz = freqPresets[freqIdx];
  Serial.printf("pattern=%s solidMask=0x%02X clk=%lukHz(src=%s) rowHold=%dus refresh=%lums(auto=%d)\n",
                patName, solidMask, (unsigned long)kHz,
                usePwm(kHz) ? "pwm" : "cpu", rowHoldUs,
                (unsigned long)refreshMs, autoRefresh);
  Serial.printf("  u12Active=%d scanShift=%d latchPerRow=%d holdAddr=%d gapInject=%luus zMode=%d(%s)\n",
                u12Active, scanDuringShift, latchPerRow, holdAddr,
                (unsigned long)gapUs, zMode, zNames[zMode]);
  if (zMode) Serial.printf("  lineSel=0x%02X segGap=%luus addrStepInGap=%d\n",
                           lineSel, (unsigned long)segGapUs, addrStepInGap);
  Serial.printf("  ctl: target=%s pulse=%s width=%luus at=%s segAddrBus=%d\n",
                ctlNames[ctlTarget], ctlPulseHigh ? "HIGH" : "LOW",
                (unsigned long)ctlPulseUs,
                ctlEveryBoundary ? "every-boundary" : "frame-start", ctlSegAddr);
  Serial.printf("  staticBus=%d\n", staticBus);
  if (lastBurstClocks) {
    float us = lastBurstCyc / (float)cpuMHz;
    float effMHz = lastBurstClocks / us;
    Serial.printf("  last burst: %lu clocks in %.1fus -> effective %.2fMHz (nominal %.2fMHz)%s\n",
                  (unsigned long)lastBurstClocks, us, effMHz, kHz / 1000.0f,
                  (!usePwm(kHz) && effMHz < kHz / 1000.0f * 0.9f)
                      ? "  << CAN'T KEEP UP, pick a slower preset" : "");
  }
}
static void help() {
  Serial.println(F("\n=== Comtel driver (v3: gap-free timed burst) ==="));
  Serial.println(F(" patterns: 0=off 1=all-on  a b c d e f = only data line 0..5"));
  Serial.println(F(" load:  [ ]=clock preset (250k..8MHz)  , . =refreshMs  m=auto-refresh"));
  Serial.println(F("        v=U12 polarity  w=scan-addr-in-shift  p=U12 latch-per-row"));
  Serial.println(F(" diag:  q=1-row load (576clk)  Q=2-row (1152clk)  j=gap inject 0/10/50us"));
  Serial.println(F("        z=cycle per-clock patterns (ALT/GRAD/ONE/SEG/LAST/SYNC; <=2MHz)"));
  Serial.println(F("        y=cycle lineSel (ALL,d0..d5): pattern lines / sync candidate"));
  Serial.println(F("        h=cycle segGap 0/2/10/50us at 576-boundaries  H=addr-step-in-gap"));
  Serial.println(F(" sync-hunt (r14/15/16 via U2): u=target none/r14/r15/r16  i=pulse polarity"));
  Serial.println(F("        k=pulse width 1/2/5/10us  o=every-boundary vs frame-start-only"));
  Serial.println(F("        r={r14,r15,r16}=segment-number WRITE-ADDRESS-BUS mode"));
  Serial.println(F("        R=hold static bus value 0..7 on {r14,r15,r16} (-1=off)"));
  Serial.println(F("        x=fire ONE burst now (use with m=auto-refresh off)"));
  Serial.println(F(" T=SCROLLING TEXT DEMO (the real driver: full-frame GFX output)"));
  Serial.println(F("        g=cycle holdAddr (freeze display on 1 addr; -1=scan)"));
  Serial.println(F(" X=strategy SWEEP (blink whole panel thru 13 loads; n/N step)"));
  Serial.println(F(" display: - + =rowHold   s=state+burst-timing  ?=help"));
}

void setup() {
  Serial.begin(115200);
  cpuMHz = getCpuFrequencyMhz();
  canvas = new GFXcanvas8(PANEL_W, PANEL_H);
  canvas->setTextWrap(false);
  canvas->setTextSize(2);                  // 12x16 glyphs fill the panel height
  pinMode(PIN_CLK, OUTPUT);
  pinMode(PIN_A, OUTPUT); pinMode(PIN_B, OUTPUT); pinMode(PIN_C, OUTPUT);
  pinMode(PIN_U12, OUTPUT);
  pinMode(PIN_R14, OUTPUT); pinMode(PIN_R15, OUTPUT); pinMode(PIN_R16, OUTPUT);
  for (int i = 0; i < 6; i++) pinMode(DATA_GPIO[i], OUTPUT);
  clkM = gbit(PIN_CLK); aM = gbit(PIN_A); bM = gbit(PIN_B); cM = gbit(PIN_C);
  u12M = gbit(PIN_U12);
  r14M = gbit(PIN_R14); r15M = gbit(PIN_R15); r16M = gbit(PIN_R16);
  applyCtlIdle();                       // r14/15/16 LOW (old grounded state)
  allDataM = 0;
  for (int i = 0; i < 6; i++) { dM[i] = gbit(DATA_GPIO[i]); allDataM |= dM[i]; }

  REG_WRITE(GPIO_OUT_W1TS_REG, clkM);   // clock idle HIGH
  u12Level(!u12Active);                 // U12 at its inactive level
  REG_WRITE(GPIO_OUT_W1TC_REG, allDataM);

  help();
  Serial.printf("cpu=%luMHz -> 5MHz preset = %lu cycles/half-period\n",
                (unsigned long)cpuMHz, (unsigned long)halfCycFor(5000));
  applyPattern();          // shift initial (off) frame
  printState();
}

static uint32_t lastShift = 0;

void loop() {
  if (sweepActive) {
    sweepStep();
    if (Serial.available()) {
      char k = Serial.read();
      while (Serial.available()) Serial.read();
      if (k == 'n') { sweepIdx = (sweepIdx + 1) % NCOMBO; needShift = true; printCombo(); }
      else if (k == 'N') { sweepIdx = (sweepIdx + NCOMBO - 1) % NCOMBO; needShift = true; printCombo(); }
      else { sweepActive = false; Serial.println(F("sweep off")); applyPattern(); }
    }
    return;
  }
  if (holdAddr >= 0) { setAddr(holdAddr); delayMicroseconds(rowHoldUs); } // freeze on 1 addr
  else displayScanOnce();  // keep the image on screen
  if (demoOn) {                             // scrolling text demo
    canvas->fillScreen(0);
    canvas->setCursor(demoX, 0);
    canvas->print(demoMsg);
    frameOut();                             // paces itself via burstGapMs
    if (--demoX < -(int)(strlen(demoMsg) * 12)) demoX = PANEL_W;
  }
  if (!demoOn && autoRefresh && millis() - lastShift >= refreshMs) { applyPattern(); lastShift = millis(); }
  if (Serial.available()) {
    char k = Serial.read();
    while (Serial.available()) Serial.read();
    bool reshift = true;
    switch (k) {
      case 'X': sweepActive = true; sweepIdx = 0; needShift = true; Serial.println(F("SWEEP: n=next N=prev, any other key=exit")); printCombo(); reshift = false; break;
      case '0': zMode = 0; solidMask = 0x00; strcpy(patName, "all off"); break;
      case '1': zMode = 0; solidMask = 0x3F; strcpy(patName, "all on"); break;
      case 'a': zMode = 0; solidMask = 0x01; strcpy(patName, "data0 only"); break;
      case 'b': zMode = 0; solidMask = 0x02; strcpy(patName, "data1 only"); break;
      case 'c': zMode = 0; solidMask = 0x04; strcpy(patName, "data2 only"); break;
      case 'd': zMode = 0; solidMask = 0x08; strcpy(patName, "data3 only"); break;
      case 'e': zMode = 0; solidMask = 0x10; strcpy(patName, "data4 only"); break;
      case 'f': zMode = 0; solidMask = 0x20; strcpy(patName, "data5 only"); break;
      case 'z': zMode = (zMode + 1) % NZ; buildZ(); reshift = false; // silent
                strncpy(patName, zMode ? "z-pattern" : "solid", sizeof(patName) - 1);
                Serial.printf("z-pattern %d/%d: %s (no burst fired; x=one-shot)\n",
                              zMode, NZ - 1, zNames[zMode]);
                break;
      case 'x': Serial.println(F("one-shot burst")); break; // reshift=true fires it
      case 'C': demoOn = false; autoRefresh = false; reshift = false;
                canvas->fillScreen(0);
                for (int mm = 0; mm < 6; mm++)         // module-ID card: line at
                  canvas->drawFastVLine(17 * mm, 0, 16, COMTEL_WHITE); // offset m
                frameOut();
                Serial.println(F("test card: module-ID vlines at x=0,17,34,51,68,85"));
                break;
      case 'T': demoOn = !demoOn; reshift = false;
                if (demoOn) { autoRefresh = false; demoX = PANEL_W; pairPhase = -1; }
                else applyCtlIdle();
                Serial.printf("scroll demo %s\n", demoOn ? "ON" : "OFF");
                break;
      case '<': if (burstGapMs > 1) burstGapMs -= (burstGapMs > 5 ? 5 : 1);
                reshift = false;
                Serial.printf("burstGap=%lums\n", (unsigned long)burstGapMs); break;
      case '>': burstGapMs += (burstGapMs >= 5 ? 5 : 1); reshift = false;
                Serial.printf("burstGap=%lums\n", (unsigned long)burstGapMs); break;
      case 'K': rowShiftLo = (rowShiftLo + 1) & 7; reshift = false;
                Serial.printf("rowShiftLo=%d\n", rowShiftLo); break;
      case 'l': ilvMode = (ilvMode + 1) % 5; reshift = false;
                Serial.printf("ilvMode=%d (0=plain 1=zigzagA 2=zigzagB 3=block2col 4=module-serial)\n",
                              ilvMode);
                break;
      case 'y': lineSel = (lineSel == 0x3F) ? 0x01
                          : (lineSel == 0x20) ? 0x3F : (lineSel << 1);
                buildZ(); reshift = false;                            // silent
                Serial.printf("lineSel=0x%02X (%s)\n", lineSel,
                              lineSel == 0x3F ? "ALL lines" : "single line");
                break;
      case 'h': segGapUs = (segGapUs == 0) ? 2 : (segGapUs == 2) ? 10
                           : (segGapUs == 10) ? 50 : 0;
                Serial.printf("segGap=%luus at each 576-clk boundary (z-patterns only)\n",
                              (unsigned long)segGapUs);
                break;
      case 'H': addrStepInGap = !addrStepInGap;
                Serial.printf("addrStepInGap=%d\n", addrStepInGap);
                break;
      case 'u': ctlTarget = (ctlTarget + 1) % 4; applyCtlIdle();
                Serial.printf("ctlTarget=%s (pulsed at segment boundaries in z-patterns)\n",
                              ctlNames[ctlTarget]);
                break;
      case 'i': ctlPulseHigh = !ctlPulseHigh; applyCtlIdle();
                Serial.printf("ctlPulse=%s\n",
                              ctlPulseHigh ? "HIGH (idle low)" : "LOW (idle HIGH)");
                break;
      case 'k': ctlPulseUs = (ctlPulseUs == 1) ? 2 : (ctlPulseUs == 2) ? 5
                             : (ctlPulseUs == 5) ? 10 : 1;
                Serial.printf("ctlPulseWidth=%luus\n", (unsigned long)ctlPulseUs);
                break;
      case 'o': ctlEveryBoundary = !ctlEveryBoundary;
                Serial.printf("ctlPulseAt=%s\n",
                              ctlEveryBoundary ? "every 576-boundary" : "frame start only");
                break;
      case 'r': ctlSegAddr = !ctlSegAddr;
                Serial.printf("ctlSegAddr=%d ({r14,r15,r16} = segment number during each 576-clk segment)\n",
                              ctlSegAddr);
                break;
      case 'R': staticBus = (staticBus >= 7) ? -1 : staticBus + 1;
                applyCtlIdle();
                if (staticBus >= 0)
                  Serial.printf("staticBus=%d held on {r14,r15,r16} all frame (r14=LSB)\n", staticBus);
                else Serial.println(F("staticBus off"));
                break;
      case '[': if (freqIdx > 0) freqIdx--; break;
      case ']': if (freqIdx < NFREQ - 1) freqIdx++; break;
      case '-': if (rowHoldUs > 5) rowHoldUs -= 10; reshift = false; break;
      case '+': rowHoldUs += 10; reshift = false; break;
      case ',': refreshMs = (refreshMs > 8) ? refreshMs / 2 : 4; reshift = false; break;
      case '.': refreshMs *= 2; reshift = false; break;
      case 'm': autoRefresh = !autoRefresh; reshift = false; break;
      case 'v': u12Active = !u12Active; break;
      case 'w': scanDuringShift = !scanDuringShift; break;
      case 'p': latchPerRow = !latchPerRow; break;
      case 'j': gapUs = (gapUs == 0) ? 10 : (gapUs == 10 ? 50 : 0); break;
      case 'q': partialLoad(1); reshift = false; break;
      case 'Q': partialLoad(2); reshift = false; break;
      case 'g': holdAddr = (holdAddr >= 7) ? -1 : holdAddr + 1; reshift = false; break;
      case 's': reshift = false; break;
      case '?': help(); reshift = false; break;
      default: reshift = false; break;
    }
    if (reshift) applyPattern();
    if (k != '?') printState();
  }
}
