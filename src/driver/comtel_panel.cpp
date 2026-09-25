// ComtelPanel implementation -- see comtel_panel.h and docs/PROTOCOL.md.
// The burst engine is the one proven during the reverse-engineering sessions:
// IRAM, interrupts masked, every clock edge scheduled off the CPU cycle
// counter (deadline-based, no drift, no gaps).
#include "comtel_panel.h"
#include <soc/gpio_reg.h>

const int ComtelPanel::DATA_PIN[6] = {16, 17, 18, 19, 21, 22}; // r1,2,4,6,7,8

static inline uint32_t ccount() {
  uint32_t c; __asm__ __volatile__("rsr %0,ccount" : "=a"(c)); return c;
}
static inline void spinUntil(uint32_t t) {
  while ((int32_t)(ccount() - t) < 0) {}
}
static inline uint32_t gbit(int g) { return 1UL << g; }

void ComtelPanel::begin() {
  cpuMHz = getCpuFrequencyMhz();
  const int outs[] = {PIN_CLK, PIN_A, PIN_B, PIN_C, PIN_U12,
                      PIN_W0, PIN_W1, PIN_W2};
  for (int p : outs) pinMode(p, OUTPUT);
  for (int i = 0; i < 6; i++) pinMode(DATA_PIN[i], OUTPUT);

  clkM = gbit(PIN_CLK); u12M = gbit(PIN_U12);
  aM = gbit(PIN_A); bM = gbit(PIN_B); cM = gbit(PIN_C);
  w0M = gbit(PIN_W0); w1M = gbit(PIN_W1); w2M = gbit(PIN_W2);
  allDataM = 0;
  for (int i = 0; i < 6; i++) { dM[i] = gbit(DATA_PIN[i]); allDataM |= dM[i]; }

  // ADDR masks for all 8 scan addresses, so the burst loop can advance the
  // display scan with two register writes and no arithmetic.
  for (int a = 0; a < 8; a++) {
    uint32_t s = 0, c = 0;
    (a & 1) ? (s |= aM) : (c |= aM);
    (a & 2) ? (s |= bM) : (c |= bM);
    (a & 4) ? (s |= cM) : (c |= cM);
    addrSet[a] = s; addrClr[a] = c;
  }

  REG_WRITE(GPIO_OUT_W1TS_REG, clkM);       // clock idles HIGH
  REG_WRITE(GPIO_OUT_W1TC_REG, u12M | allDataM | w0M | w1M | w2M);
  scanAddr = 0;
  setAddr(0);
}

void ComtelPanel::setAddr(uint8_t a) {
  a &= 7;
  REG_WRITE(GPIO_OUT_W1TS_REG, addrSet[a]);
  REG_WRITE(GPIO_OUT_W1TC_REG, addrClr[a]);
}

void ComtelPanel::setWriteAddr(uint8_t v) {
  uint32_t s = 0, c = 0;
  (v & 1) ? (s |= w0M) : (c |= w0M);
  (v & 2) ? (s |= w1M) : (c |= w1M);
  (v & 4) ? (s |= w2M) : (c |= w2M);
  REG_WRITE(GPIO_OUT_W1TS_REG, s);
  REG_WRITE(GPIO_OUT_W1TC_REG, c);
}

// canvas -> the pair's 576 per-clock GPIO masks (module-serial layout).
// The 3 clocks of each pixel triplet drive the Blue, Green, Red subpixels in
// that order; each carries 0x3F if its channel bit is set in the canvas color
// (bit0=R, bit1=G, bit2=B), else 0x00. So the three clocks may now differ.
void ComtelPanel::buildPair(int p) {
  static const uint8_t clockBit[3] = {2, 1, 0};  // clock 0=B, 1=G, 2=R -> bit
  int yLower = 15 - ((p + rowShiftLo) & 7);   // canvas y: 0 = top row
  int yUpper = 7 - p;
  for (int j = 0; j < PAIR_CLOCKS; j += 3) {
    int t = j / 3, q = t & 31;                // triplet; index within module
    int col = ((t >> 5) << 4) + (q & 15);     // module*16 + offset
    int y = (q < 16) ? yLower : yUpper;
    uint8_t color = canvas.getPixel(col, y) & 7;
    for (int k = 0; k < 3; k++) {             // k = subpixel clock (B, G, R)
      uint8_t v = (color & (1 << clockBit[k])) ? 0x3F : 0x00;
      uint32_t s = 0, c = 0;
      for (int i = 0; i < 6; i++) ((v >> i) & 1) ? (s |= dM[i]) : (c |= dM[i]);
      pairSet[j + k] = s; pairClr[j + k] = c;
    }
  }
}

// one write burst: U12 high, a whole number of 576-clock payloads clocked
// gap-free (the register keeps the last 576), U12 drop latches.
//
// The shift is scan-address-independent -- only the latching U12 edge consults
// ADDR -- so the burst keeps multiplexing the display through its own clocks:
// the clocks are split into 8 equal segments and ADDR advances at each
// boundary, in the clock-low window. Without that, ADDR sits frozen for the
// whole burst and its pair burns ~6x the duty of every other row.
void IRAM_ATTR ComtelPanel::burstPair(uint32_t halfCyc) {
  uint32_t reps = burstClocks / (uint32_t)PAIR_CLOCKS;
  if (reps < 1) reps = 1;                     // a partial payload cannot latch
  const uint32_t clocks = reps * (uint32_t)PAIR_CLOCKS;
  const uint32_t seg = scanDuringBurst ? (clocks / 8) : 0;  // 0 = don't scan
  // ADDR = 0 hold before the latch: the longer of the old edge margin and
  // latchSettleUs, in CPU cycles.
  uint32_t settle = latchSettleUs * cpuMHz;
  if (settle < 4 * halfCyc) settle = 4 * halfCyc;

  noInterrupts();
  REG_WRITE(GPIO_OUT_W1TS_REG, u12M);
  uint32_t t = ccount() + 4 * halfCyc;
  spinUntil(t);
  // First slice starts at k = 0, so the burst advances the rolling scan by
  // exactly 8 -- which, with the +1 from show()'s buildPair() dwell, walks the
  // dwell one address further on every pair. Over a frame's 8 bursts every
  // address takes that dwell exactly once.
  uint32_t nextSeg = 0;
  for (uint32_t k = 0, j = 0; k < clocks; k++) {
    REG_WRITE(GPIO_OUT_W1TC_REG, clkM);       // falling edge...
    REG_WRITE(GPIO_OUT_W1TS_REG, pairSet[j]); // ...data changes while low
    REG_WRITE(GPIO_OUT_W1TC_REG, pairClr[j]);
    if (++j == PAIR_CLOCKS) j = 0;
    if (seg && k == nextSeg) {                // hand the next scan address its
      REG_WRITE(GPIO_OUT_W1TS_REG, addrSet[scanAddr]);   // slice of the burst
      REG_WRITE(GPIO_OUT_W1TC_REG, addrClr[scanAddr]);
      scanAddr = (scanAddr + 1) & 7;
      nextSeg += seg;
    }
    t += halfCyc; spinUntil(t);
    REG_WRITE(GPIO_OUT_W1TS_REG, clkM);       // rising edge samples data
    t += halfCyc; spinUntil(t);
  }
  REG_WRITE(GPIO_OUT_W1TS_REG, addrSet[0]);   // destination = (ADDR - bus) mod
  REG_WRITE(GPIO_OUT_W1TC_REG, addrClr[0]);   // 8, sampled at the latch edge:
  t += settle; spinUntil(t);                  // ADDR must read 0, settled
  REG_WRITE(GPIO_OUT_W1TC_REG, u12M);         // latch
  interrupts();
}

void ComtelPanel::service() {
  // Eight addresses per pass, resuming wherever the last pass (or the last
  // burst) left the rolling scan, so no address is ever short-changed.
  for (uint8_t i = 0; i < 8; i++) {
    setAddr(scanAddr);
    scanAddr = (scanAddr + 1) & 7;
    delayMicroseconds(rowHoldUs);
  }
}

void ComtelPanel::show() {
  uint32_t halfCyc = (cpuMHz * 1000UL) / (2UL * clockKHz);
  if (halfCyc < 60) halfCyc = 60;             // CPU engine floor (~2 MHz)
  for (int p = 0; p < 8; p++) {
    setWriteAddr((8 - p) & 7);                // dest pair = (8 - bus) mod 8
    delayMicroseconds(2);                     // with ADDR = 0 at latch time,
                                              // which burstPair() guarantees.
    if (scanDuringBurst) {
      // buildPair() is ~0.1 ms of pure compute with ADDR frozen wherever it
      // was; spend it as one legitimate scan dwell so it steals no display
      // time from any single address.
      setAddr(scanAddr);
      scanAddr = (scanAddr + 1) & 7;
      uint32_t t0 = micros();
      buildPair(p);
      uint32_t used = micros() - t0;
      if (used < (uint32_t)rowHoldUs) delayMicroseconds(rowHoldUs - used);
    } else {
      setAddr(0);                             // pre-2026-07-26 path: ADDR
      buildPair(p);                           // frozen at 0 across compute
    }                                         // and burst (the bright-row bug)
    burstPair(halfCyc);
    REG_WRITE(GPIO_OUT_W1TC_REG, allDataM);
    uint32_t until = millis() + interBurstMs; // keep the display fed
    while ((int32_t)(millis() - until) < 0) service();
  }
  setWriteAddr(0);
}
