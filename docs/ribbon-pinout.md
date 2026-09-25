# Comtel sign — 20-pin ribbon reference (final, verified)

The sign's input connector, as actually decoded and driven by the working
firmware. Signal meanings: [PROTOCOL.md](PROTOCOL.md). How to wire an ESP32
to it: [wiring.md](wiring.md).

## Pin numbering

Red stripe = **pin 1**. Standard 2×10 IDC zig-zag:

```
        red stripe edge
            |
            v
   pin  1   3   5   7   9  11  13  15  17  19
   pin  2   4   6   8  10  12  14  16  18  20
```

## Full pin map

| Ribbon | Signal | Direction | Notes |
|-------:|--------|-----------|-------|
| 1  | **D0** — brightness bit 0 (LSB) | in | worth 1/63 of full brightness |
| 2  | **D1** — brightness bit 1 | in | |
| 3  | not connected | — | |
| 4  | **D2** — brightness bit 2 | in | |
| 5  | not connected | — | |
| 6  | **D3** — brightness bit 3 | in | |
| 7  | **D4** — brightness bit 4 | in | |
| 8  | **D5** — brightness bit 5 (MSB) | in | worth 32/63 |
| 9  | GND | — | |
| 10 | **CLK** — data clock | in | idles HIGH; data sampled on rising edge; original ran 5 MHz, ≥2 MHz works |
| 11 | GND | — | |
| 12 | **U12** — shift-enable / latch | in | HIGH while shifting; falling edge latches the last 576 values into a row-pair |
| 13 | GND | — | |
| 14 | **W0** — write-address bit 0 (LSB) | in | destination pair = (8 − W) mod 8, sampled at the U12 latch |
| 15 | **W1** — write-address bit 1 | in | grounded W = 0 = "always pair 0" (the original blocker!) |
| 16 | **W2** — write-address bit 2 | in | |
| 17 | +5 V strap (panel's 74HC245 DIR) | out | leave unconnected — do NOT drive |
| 18 | **ADDR A** — display-scan bit 0 | in | display multiplex only; scan 0–7 continuously (~205 µs each) |
| 19 | **ADDR B** — display-scan bit 1 | in | |
| 20 | **ADDR C** — display-scan bit 2 | in | does NOT select the write destination |

## The two address buses (don't mix them up)

- **ADDR A/B/C (18/19/20)** choose which row-pair is *shown* right now.
  Scan them 0→7 forever or the display goes dark.
- **W0/W1/W2 (14/15/16)** choose which row-pair the next write *lands in*.
  These were once believed unused and tied to ground — which silently
  addressed every write to pair 0 and stalled the project for weeks.

## History

This file was originally the multimeter trace worksheet from the
reverse-engineering effort (the trace data itself is preserved in
[../archive/STATUS.md](../archive/STATUS.md) and the story in
[HISTORY.md](HISTORY.md)). Signals once labelled "spare/unused" are the
write-address bus above.
