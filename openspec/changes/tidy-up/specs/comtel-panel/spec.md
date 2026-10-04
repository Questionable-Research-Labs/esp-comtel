## ADDED Requirements

### Requirement: Driver construction and lifecycle

The shared driver SHALL be the `ComtelPanel` class in `src/driver/comtel_panel.h/.cpp`. A plugin SHALL construct one `ComtelPanel`, call `begin()` once during setup, call `service()` continuously from its loop, and call `show()` whenever it wants the canvas pushed to the glass. `begin()` SHALL configure every sign-signal GPIO as an output and leave the bus idle (clock high, data/U12/write-address low, scan address 0).

#### Scenario: Minimal plugin lifecycle
- **WHEN** a plugin constructs `ComtelPanel`, calls `begin()`, draws on `panel.canvas`, and calls `show()`
- **THEN** the drawn frame appears on the sign
- **AND** it stays lit as long as the plugin keeps calling `service()`

#### Scenario: Display needs continuous servicing
- **WHEN** a plugin stops calling `service()` (or `show()`) for a sustained period
- **THEN** the display stops multiplexing and the glass shows at most one scan address's rows

### Requirement: RGB on/off canvas framebuffer

The driver SHALL expose a 96×16 Adafruit `GFXcanvas8` as `panel.canvas`, one byte per pixel (1536 bytes). Only the low three bits of each pixel SHALL affect output, as an RGB on/off bitmask (bit 0 = red, bit 1 = green, bit 2 = blue). Canvas `(0, 0)` SHALL be the top-left pixel of the sign in its normal landscape orientation. `ComtelPanel::W` and `ComtelPanel::H` SHALL be 96 and 16.

#### Scenario: Standard GFX drawing
- **WHEN** a plugin calls GFX methods (`drawPixel`, `fillRect`, `drawLine`, `print`, `fillScreen`) on `panel.canvas`
- **THEN** the result is shown on the sign at the next `show()`

#### Scenario: High bits ignored
- **WHEN** a pixel holds a value above 7
- **THEN** it is displayed as its low three bits

#### Scenario: Raw buffer access
- **WHEN** a plugin writes `panel.canvas.getBuffer()` directly as 96 bytes per row, row-major from the top row
- **THEN** the result is identical to drawing the same values with `drawPixel`

### Requirement: Fixed eight-color palette

`comtel_panel.h` SHALL define the palette constants below, and plugins SHALL use them rather than bare numbers. Each set channel SHALL be driven at full hardware brightness; there SHALL be no intermediate levels.

| Constant | Value | Channels |
|---|---|---|
| `COMTEL_OFF` | 0 | none |
| `COMTEL_RED` | 1 | R |
| `COMTEL_GREEN` | 2 | G |
| `COMTEL_YELLOW` | 3 | R+G |
| `COMTEL_BLUE` | 4 | B |
| `COMTEL_MAGENTA` | 5 | R+B |
| `COMTEL_CYAN` | 6 | G+B |
| `COMTEL_WHITE` | 7 | R+G+B |

#### Scenario: Mixed color
- **WHEN** a plugin draws a pixel with `COMTEL_YELLOW`
- **THEN** that dot's red and green subpixels light at full brightness and blue stays off

#### Scenario: Legacy "on" value
- **WHEN** a plugin draws with a bare `1` (the old 1 bpp "on")
- **THEN** the pixel is red only, not white

### Requirement: Per-channel subpixel mapping

When building a pair's shift data, the driver SHALL map each pixel to three consecutive data clocks in the order Blue, Green, Red, each carrying `0x3F` on all six data lines if that channel bit is set and `0x00` otherwise. Pair `p` SHALL carry canvas row `7 - p` (upper half) and canvas row `15 - ((p + rowShiftLo) & 7)` (lower half), with columns laid out module-serially across the six 16-column modules.

#### Scenario: Cyan pixel
- **WHEN** the canvas holds `COMTEL_CYAN` at a dot
- **THEN** that dot's blue and green clocks carry `0x3F` and its red clock carries `0x00`

#### Scenario: Off pixel
- **WHEN** the canvas holds `COMTEL_OFF` at a dot
- **THEN** all three of its clocks carry `0x00`

### Requirement: Frame write via eight latched pair bursts

`show()` SHALL write the whole canvas as eight pair bursts. For each pair it SHALL set the write address to `(8 - p) & 7`, build the pair data, and clock a gap-free burst with U12 high, consisting of a whole number of 576-clock payloads (`burstClocks` rounded down to a multiple of 576, minimum one payload). The burst SHALL run with interrupts masked and clock edges scheduled from the CPU cycle counter. Before the U12 falling edge that latches the pair, the scan address SHALL be driven to 0 and held for at least `latchSettleUs`. Between bursts, `show()` SHALL keep the display multiplexed for `interBurstMs`.

#### Scenario: Pair lands in the right rows
- **WHEN** `show()` writes all eight pairs
- **THEN** every canvas row appears on its own physical row, with no pair shifted by one

#### Scenario: Short burst still latches
- **WHEN** `burstClocks` is set below 576
- **THEN** one full 576-clock payload is still sent

### Requirement: Equal display duty for every row

The driver SHALL give each of the eight scan addresses an equal share of display time, so all 16 rows are equally bright. `service()` SHALL step through eight addresses per call, dwelling `rowHoldUs` on each and resuming from wherever the previous scan or burst left off (rolling scan address). When `scanDuringBurst` is true, each burst SHALL split its clocks into eight equal segments and advance the scan address at each boundary, and the pair-build time inside `show()` SHALL be spent as one normal scan dwell. When `scanDuringBurst` is false, the address SHALL stay parked at 0 for the build and burst (the pre-2026-07-26 behavior, kept for A/B comparison).

#### Scenario: Uniform field
- **WHEN** a full-white frame is shown and refreshed repeatedly with default calibration
- **THEN** all 16 rows appear equally bright to the eye (a residual ~2% bias toward rows 0 and 8 from the latch settle window is acceptable)

#### Scenario: Legacy bright-row path
- **WHEN** `scanDuringBurst` is false and `burstClocks` is 4608
- **THEN** the driver behaves as before the duty fix, and physical rows 0 and 8 are visibly brighter

### Requirement: Calibration fields with verified defaults

The driver SHALL expose these public calibration fields, with defaults equal to the values verified on hardware. Plugins MAY change them at runtime; the driver SHALL NOT persist them.

| Field | Default | Meaning |
|---|---|---|
| `rowShiftLo` | 0 | lower-half row-decoder offset (0..7) |
| `clockKHz` | 2000 | burst data clock (floored at about 2 MHz by the CPU engine) |
| `rowHoldUs` | 205 | display dwell per scan address |
| `interBurstMs` | 2 | display keep-alive between pair bursts |
| `burstClocks` | 1152 | burst length (two payloads) |
| `scanDuringBurst` | true | multiplex during bursts |
| `latchSettleUs` | 10 | ADDR = 0 hold before latch |

#### Scenario: Defaults need no tuning
- **WHEN** a plugin uses `ComtelPanel` without touching any calibration field
- **THEN** the sign displays correctly with uniform row brightness

#### Scenario: Runtime A/B
- **WHEN** a plugin changes `burstClocks` or `scanDuringBurst` between frames
- **THEN** the next `show()` uses the new values without re-calling `begin()`

### Requirement: Fixed driver GPIO map

The driver SHALL own these ESP32 GPIOs, and no plugin SHALL use them: clock 23; scan address A/B/C 25, 26, 27; U12 latch 13; write address W0/W1/W2 4, 5, 14; data lines 16, 17, 18, 19, 21, 22. All driver pins SHALL be below 32, so they can be driven through the single `GPIO_OUT` set/clear registers.

#### Scenario: Pin conflict avoided
- **WHEN** a plugin needs a GPIO for input
- **THEN** it chooses from the pins declared in `button_pins.h`, none of which are in the driver map
