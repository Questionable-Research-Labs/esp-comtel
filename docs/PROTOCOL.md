# Comtel 96×16 sign — complete drive protocol

> Reverse-engineered 2026-07-03 → 2026-07-08 (scope work + on-glass experiments).
> Verified end-to-end: scrolling text on all 16 rows. This is the authoritative
> spec; [HISTORY.md](HISTORY.md) tells the story of how it was derived (the
> raw running log of the hunt is preserved in [../archive/STATUS.md](../archive/STATUS.md)).

## Electrical

- 20-pin ribbon (red stripe = pin 1, 2×10 zig-zag). All logic 5 V — drive
  through 74HCT245s from a 3.3 V ESP32. Common ground (ribbon 9/11/13).
- Panels self-power their LEDs; the ribbon is signals + ground only.

| Ribbon | Signal |
|---:|---|
| 1,2,4,6,7,8 | D0–D5: subpixel **brightness bits**, D0 (r1) = LSB … D5 (r8) = MSB |
| 10 | CLK — idles HIGH, data sampled on RISING edge; original runs 5 MHz, works ≥2 MHz |
| 12 | U12 — shift-enable/latch: HIGH during shift, **falling edge latches** |
| 14,15,16 | **write-address bus** W0,W1,W2 (r14 = LSB) |
| 18,19,20 | ADDR A,B,C — **display scan** address (read side only) |
| 17 | +5 V strap (panel's 245 DIR); leave NC |
| 3,5 | NC |

## Architecture

- 6 × 16×16 RGB modules, daisy-chained → 96×16.
- The LED drivers (LD71D1048) hold a **persistent frame buffer** organized as
  8 **row-pairs**. Pair p covers two physical rows (see Row mapping).
- A 576-stage shift register holds one pair's worth of data. It keeps the
  **last 576 clocks** received; anything earlier shifts through and is lost.
- Rows that have never been written light up fully (hardware default), and
  scan-address lines do NOT gate that — an unwritten panel is fully lit.

## Writing one row-pair

1. Set the write-address bus: **{r14,r15,r16} = (8 − p) mod 8** (r14 = LSB).
   (Grounded bus = value 0 = pair 0 — why a sign with these pins grounded only
   ever accepts writes to pair 0.)
2. **Park ADDR A/B/C at 0.** The latch destination is sampled at latch time
   and tracks the display address: all verified writes used ADDR = 0; bursts
   fired with ADDR = 7 land every pair one lower (observed on glass
   2026-07-15 — two blank rows + displaced content). Best-fit rule:
   **destination = (ADDR − bus) mod 8**.
3. Raise U12.
4. Clock in ≥576 data values (one per rising edge). Sending the pair's 576
   values repeated 8× (=4608 clocks, matching the original's burst) is fine.
5. Drop U12 → the register contents latch into pair p. Content persists until
   overwritten (no periodic reload needed).

A full frame = 8 such bursts.

## Pair data layout (576 clocks)

Module-serial: six 96-clock chunks, one per 16-column module, **left → right**:

```
chunk m (96 clocks) = module m (columns 16m … 16m+15):
  16 pixels of the pair's LOWER row, columns left → right
  16 pixels of the pair's UPPER row, columns left → right
each pixel = 3 clocks = B, G, R   (6-bit brightness per channel on D0–D5)
```

So clock index j (0–575): triplet t = j∕3, module = t∕32, q = t mod 32;
row = lower if q<16 else upper; column = 16·module + (q mod 16);
channel = j mod 3 → B,G,R.

Brightness is 6-bit linear duty; the eye compresses hard — values ≥ ~8 look
"lit", ≤ 2 look off. For on/off content use 0x3F / 0x00.

## Row mapping

- Pair p, **upper** row = physical row p+8 (counting from the bottom, 0-based).
- Pair p, **lower** row = physical row **p** — both halves are straight
  (`rowShiftLo = 0`). Verified with a per-row staircase pattern on glass,
  2026-07-15. The "lower half off by one" belief from the probe era was an
  artifact of those sessions' write conditions (destination tracks ADDR at
  latch time — see Writing above), not real decoder behavior.

## Displaying

The panel only lights rows whose scan address is presented on ADDR A/B/C
(active continuously, ~205 µs per address, all 8 addresses ≈ 610 Hz like the
original). Address a lights the corresponding pair's two rows from the frame
buffer. Keep scanning forever; pause only during write bursts.

## Timing that works (ESP32, CPU bit-bang engine)

- Data clock 2 MHz (cycle-counter-timed, interrupts masked, IRAM). The
  original's 5 MHz needs DMA/I2S for per-clock data — 2 MHz is fine.
- Burst = a whole number of 576-clock payloads. The original sent 8 copies
  (4608 clocks ≈ 2.3 ms); 2 copies (1152 ≈ 0.6 ms) is the driver default —
  the register only keeps the last 576, so the extra copies are pure margin.
- Full frame ≈ 33 ms at 1152-clock bursts (was ≈ 45 ms at 4608), of which
  ~26 ms is display-scan keep-alive between bursts → ~30 fps.

## Duty cycle: a burst steals display time

**A write burst freezes the display on whichever address is presented**, because
the burst holds ADDR still (it must read 0 at latch time) with interrupts
masked. That time is not neutral — the addressed pair's two rows stay lit for
the whole burst. Get this wrong and the sign shows it:

> Parking ADDR at 0 for all eight bursts of a frame (the driver's behaviour
> until 2026-07-26) gave address 0 ≈ 19 ms of a measured 41.9 ms frame while
> addresses 1–7 got ≈ 2.9 ms each. Pair 0 — **physical rows 0 and 8** — ran at
> ~6× the duty of every other row. Both rows were visibly brighter on glass
> (verified 2026-07-26 by toggling `scanDuringBurst`: the artifact appears and
> disappears with it).

Rules for any driver:

1. **Do not freeze on one address for the length of a burst.** The shift is
   scan-address-independent — only the latching U12 edge consults ADDR — so ADDR
   may be advanced freely *during* the shift. The driver splits each burst into
   8 equal clock segments, one per address, changing ADDR in the clock-low
   window (`ComtelPanel::burstPair`, `scanDuringBurst`).
2. **Restore ADDR = 0 and let it settle before dropping U12** (`latchSettleUs`,
   10 µs) so the latch destination is unambiguous.
3. **Account for compute too.** Building a pair's 576 masks is ~0.1 ms with ADDR
   frozen; spend it inside a legitimate scan dwell, not as a bonus for one
   address. Eight such dwells per frame must land on eight different addresses.
4. **Keep bursts short.** Every clock beyond one payload is display time spent
   on a single address.

Residual imbalance in the current driver: address 0 gets `latchSettleUs` × 8 ≈
80 µs per frame more than the others (~2%), which is below the eye's threshold.

**Shortening the burst alone is not enough.** Measured on glass 2026-07-26 with
the `row_uniformity` test card:

| Calibration | Frame | Uniform white field |
|---|---:|---|
| 4608 clocks, ADDR parked (pre-fix) | 41.9 ms | rows 0 + 8 clearly brighter |
| 1152 clocks, ADDR parked | 25.4 ms | rows 0 + 8 still clearly brighter |
| 1152 clocks, scanning in-burst | 25.7 ms | **uniform** |
| 576 clocks, scanning in-burst | 24.8 ms | uniform; no gain over 1152 |

Rule 1 is what fixes it; the shorter burst is what makes the frame faster. Going
below 1152 buys nothing measurable — once the burst is short, the keep-alive
scan (~19 ms/frame) dominates — so 1152 is the default and 576 is untested for
long-run latch reliability. In-burst ADDR movement does **not** disturb the
latch: a per-row staircase lands every pair correctly with `latchSettleUs = 10`
(verified on glass 2026-07-26). `scanDuringBurst = false` plus
`burstClocks = 4608` reproduces the old behaviour for comparison.

## Historical traps (so nobody falls in again)

- The six data lines are NOT colour planes and NOT one serial stream — they
  are a parallel 6-bit brightness value.
- "Frame buffer decays toward ON" was wrong — written content persists.
- Multi-pair writes observed during early experiments came from bursts fired
  at each intermediate bus value by the test scripts, not from any pointer.
- ESP32 auto-resets on serial-port open (DTR) — toggle state is lost;
  scripts must re-establish and verify state every session.
