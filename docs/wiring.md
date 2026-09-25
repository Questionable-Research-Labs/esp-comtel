# Wiring the ESP32 to the Comtel sign — from scratch

A complete guide for rebuilding this wiring from nothing. No electronics
background assumed beyond "can follow a pin table." The finished, verified
setup is exactly what the firmware in this repo expects.

## What you need

- 1× ESP32 devkit board ("esp32dev" / WROOM-32 style)
- 2× **74HCT245** chips (they translate the ESP32's 3.3 V signals to the
  sign's 5 V — must be the **HCT** variant, and the ESP32 must never touch
  5 V directly)
- 2× push buttons (optional, for the controls)
- The sign's 20-pin ribbon cable / IDC connector, breadboard, jumper wires

## The big picture

```
   ESP32  --3.3V signals-->  [74HCT245 "U1"]  --5V signals-->  ribbon --> SIGN
          --3.3V signals-->  [74HCT245 "U2"]  --5V signals-->
```

Fourteen ESP32 pins carry the sign's signals. Each goes **into** one of the
two translator chips (their "A" side) and comes **out** at 5 V (their "B"
side) onto a ribbon pin. The sign keeps its own power supply for the LEDs —
the ribbon carries only signals and ground.

## Step 1 — set up the two translator chips

74HCT245 pin numbering (the notch/dot marks pin 1, counting counter-clockwise):

```
            74HCT245
     DIR  1 ┌──┐ 20  VCC
     A1   2 │  │ 19  /OE
     A2   3 │  │ 18  B1
     A3   4 │  │ 17  B2
     A4   5 │  │ 16  B3
     A5   6 │  │ 15  B4
     A6   7 │  │ 14  B5
     A7   8 │  │ 13  B6
     A8   9 │  │ 12  B7
     GND 10 └──┘ 11  B8
```

Channel A1 pairs with B1, A2 with B2, and so on. **Both chips** get these
four fixed connections before anything else:

| Chip pin | Connect to | Why |
|---|---|---|
| 1 (DIR) | +5 V | sets direction: A-side in, B-side out |
| 19 (/OE) | GND | keeps the outputs switched on |
| 20 (VCC) | +5 V | powers the chip at 5 V |
| 10 (GND) | GND | ground |

+5 V comes from the ESP32 board's **5V/VIN pin** (the chips draw almost
nothing). Ground is shared between everything — see Step 4.

## Step 2 — chip U1 (data, clock, first address line)

| ESP32 GPIO | Signal | into U1 pin | out of U1 pin | → ribbon pin |
|---:|---|:---:|:---:|:---:|
| 16 | D0 (brightness bit 0) | 2 (A1) | 18 (B1) | 1 |
| 17 | D1 | 3 (A2) | 17 (B2) | 2 |
| 18 | D2 | 4 (A3) | 16 (B3) | 4 |
| 19 | D3 | 5 (A4) | 15 (B4) | 6 |
| 21 | D4 | 6 (A5) | 14 (B5) | 7 |
| 22 | D5 (brightness bit 5) | 7 (A6) | 13 (B6) | 8 |
| 23 | CLK | 8 (A7) | 12 (B7) | 10 |
| 25 | ADDR A | 9 (A8) | 11 (B8) | 18 |

## Step 3 — chip U2 (addresses, latch, write-address bus)

| ESP32 GPIO | Signal | into U2 pin | out of U2 pin | → ribbon pin |
|---:|---|:---:|:---:|:---:|
| 26 | ADDR B | 2 (A1) | 18 (B1) | 19 |
| 27 | ADDR C | 3 (A2) | 17 (B2) | 20 |
| 13 | U12 (latch) | 4 (A3) | 16 (B3) | 12 |
| 4  | W0 (write-address bit 0) | 5 (A4) | 15 (B4) | 14 |
| 5  | W1 | 6 (A5) | 14 (B5) | 15 |
| 14 | W2 | 7 (A6) | 13 (B6) | 16 |
| —  | (unused input) | 8 (A7) → **GND** | — | — |
| —  | (unused input) | 9 (A8) → **GND** | — | — |

The two unused inputs are tied to ground so they can't pick up noise.

> **Do not tie ribbon 14/15/16 to ground.** An older version of this guide
> said to. Those three wires are the sign's *write-address* — grounding them
> silently sends every write to the same two rows. That mistake cost weeks;
> read [HISTORY.md](HISTORY.md) for the saga.

## Step 4 — power, ground, and the ribbon

- **+5 V**: ESP32 board 5V/VIN pin → both chips' pin 20 (VCC) and pin 1 (DIR).
- **Ground (everything to everything)**: ESP32 GND ↔ both chips' pins 10 and
  19 ↔ U2 pins 8 and 9 ↔ the sign's ground (ribbon pin 9, 11 or 13).
- **Ribbon pin 17**: leave completely unconnected (the sign puts its own
  +5 V there).
- **Ribbon pins 3 and 5**: not connected inside the sign; ignore.
- The sign's own power supplies stay connected as always — the ESP32 never
  powers any LEDs.

Ribbon numbering (red stripe = pin 1, standard 2×10 zig-zag — full signal
list in [ribbon-pinout.md](ribbon-pinout.md)):

```
        red stripe edge
            |
            v
   pin  1   3   5   7   9  11  13  15  17  19
   pin  2   4   6   8  10  12  14  16  18  20
```

## Step 5 — buttons (optional)

Up to six buttons are supported. Every button wires the **same way** — one leg
to its GPIO, the other leg to GND — so a press pulls the pin LOW. What differs
is only whether the pin needs an external pull-up resistor:

| Button | GPIO | Between | And | Pull-up | Notes |
|---|---:|---|---|---|---|
| BTN1 | 32 | GPIO 32 | GND | internal | next message · long: pause/resume |
| BTN2 | 33 | GPIO 33 | GND | internal | speed · long: test card |
| BTN3 | 15 | GPIO 15 | GND | internal | **strapping pin** — see note |
| BTN4 | 2  | GPIO 2  | GND | internal | **strapping pin** — see note |
| BTN5 | 34 | GPIO 34 | GND | **external 10 kΩ → 3.3 V** | input-only, no internal pull-up |
| BTN6 | 35 | GPIO 35 | GND | **external 10 kΩ → 3.3 V** | input-only, no internal pull-up |

BTN1–BTN4 need **no resistor** — the firmware enables the ESP32's internal
pull-ups. GPIO 34 and 35 are **input-only** and have *no* internal pull-up, so
each needs its own external **10 kΩ resistor from the pin to 3.3 V** to hold the
line high when the button is open:

```
   3.3 V ──[ 10 kΩ ]──┬── GPIO 34 (or 35)
                      │
                    button
                      │
                     GND
```

> **Strapping pins — GPIO 2 and 15.** These are read by the ESP32 at reset to
> pick the boot mode. A button *held down at reset* pulls them LOW and can block
> normal boot or download mode. It's fine to use them as buttons — just don't
> hold BTN3/BTN4 while the board resets or while flashing.

GPIO 32 and 33 are the two primary wired buttons. The authoritative, current
list of pins reserved for button input — the primary pair, the extra
internal-pull-up pins (GPIO 2 and 15), and the input-only GPIO 34/35/36/39 that
*do* need an external pull-up — lives in
[`src/driver/button_pins.h`](../src/driver/button_pins.h). New plugins should
use the named constants there rather than hard-coding pin numbers. The
`button_test` plugin exercises all six pins above (one coloured square per
button) and is the quickest way to confirm the wiring.

## Step 6 — checklist, then power on

- [ ] Original Comtel controller unplugged from the sign
- [ ] Both chips: DIR→5 V, /OE→GND, VCC→5 V, GND→GND
- [ ] U2 pins 8 and 9 grounded; ribbon 14/15/16 **driven, not grounded**
- [ ] One common ground: ESP32, both chips, sign
- [ ] Ribbon pin 17 not connected
- [ ] If using BTN5/BTN6: a 10 kΩ resistor from GPIO 34→3.3 V and GPIO 35→3.3 V
- [ ] No button on a strapping pin (GPIO 2/15) held down during reset/flash
- [ ] Red-stripe orientation double-checked before plugging the ribbon in
- [ ] Flash: `pio run -e drive -t upload --upload-port COM4`
- [ ] Open a serial monitor @115200 and type a message

If text appears but looks vertically misaligned, see `!shift` in the README
(the sign's lower half has a one-row decoder offset; the default of 1 is
correct for this panel).

## Troubleshooting the flash

**`A fatal error occurred: Failed to connect to ESP32: No serial data
received.`** — esptool opened the port but the chip never answered. If this
starts happening *right after you closed a serial monitor*, the culprit is a
**latched DTR/RTS line**: killing a monitor can leave the CP2102 asserting the
auto-reset pins, holding the ESP32 wedged in reset. It is **not** your wiring or
a strapping pin — verified 2026-07-25 that unplugging the buttons and even
holding BOOT both still failed, while the fix below worked first try.

- **Fix: fully unplug the ESP32's USB cable, wait ~5 s, plug it back in**, then
  reflash. The power-cycle clears the stuck line states and auto-reset returns
  (you'll see `Hard resetting via RTS pin...` on success).
- Other causes of the same message: a strapping-pin button (GPIO 2/15) held
  down at reset (don't); a genuine serial monitor still holding the port (that
  reports *port busy* instead); or a board with no auto-reset (hold BOOT while
  esptool prints `Connecting…`).
