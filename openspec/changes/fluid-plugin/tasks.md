## 1. Plugin scaffold

- [x] 1.1 Create `src/plugins/fluid.cpp` with `// @plugin: Fluid` descriptor, `#include "../driver/comtel_panel.h"` and `button_pins.h`, static `ComtelPanel panel`, and `setup()`/`loop()` that call `panel.begin()` and `panel.service()`
- [x] 1.2 Run `python tools/flash.py --sync` and confirm `[env:fluid]` appears in `platformio.ini`
- [x] 1.3 Compile-check: `python tools/flash.py fluid --no-upload` (or `pio run -e fluid`)

## 2. Occupancy grid and rendering

- [x] 2.1 Add a 16×96 `uint8_t` occupancy grid (low 3 bits = R/G/B) and helpers to get/set a channel bit
- [x] 2.2 Implement `renderFrame()`: `fillScreen(COMTEL_OFF)`, draw each occupied cell with the matching `COMTEL_*` value, then `panel.show()`
- [x] 2.3 Verify compile; optional bring-up: fill a few cells with red/green overlap (yellow) before enabling motion

## 3. Fluid step

- [x] 3.1 Step particles bottom-up: try down, then random diagonal-down, then random sideways; move a channel bit only into a cell that does not already hold that channel
- [x] 3.2 Drive the step from `loop()` on a ~40 ms interval; always call `panel.service()`, redraw after each step
- [x] 3.3 Seed `random()` (or `esp_random`) so left/right and spawn positions vary per boot

## 4. Spawning

- [x] 4.1 Implement `dropBlock(primary, x)` for a 4×3 rectangle at the top, clipped on-canvas, writing only into cells that lack that primary
- [x] 4.2 Auto-spawn: every ~450 ms, while a scene drain is not active, drop a random primary at a random X
- [x] 4.3 Keep block size and spawn interval as named constants so fill rate can be tuned toward ~60 s to fill with drain off

## 5. Draining

- [x] 5.1 Implement `shiftDown()`: discard row 15, move rows 0..14 down one, clear row 0
- [x] 5.2 Auto-drain: while occupied-cell count ≥ 768, call `shiftDown()` on a cadence that holds fill near half (~one packed row every 3–4 s as a starting point)
- [x] 5.3 Scene drain: a flag that pauses auto-spawn, ignores colour drops, and shifts down every step until the grid is empty, then resumes auto-spawn

## 6. Buttons

- [x] 6.1 Configure GPIO 32, 33, 2, 15 as `INPUT_PULLUP` after `panel.begin()`; 20 ms debounce; arm input only after all four have been seen released
- [x] 6.2 Press edge: 32 drops red, 33 drops green, GPIO 2 drops blue (same `dropBlock` as auto-spawn, random X)
- [x] 6.3 Press edge: GPIO 15 starts a scene drain

## 7. Documentation

- [x] 7.1 Update `README.md` to list the `fluid` plugin and its four-button map

## 8. Verify

- [ ] 8.1 `python tools/flash.py fluid` builds and uploads
- [ ] 8.2 On hardware (landscape): blobs fall, spill, and mix (yellow / cyan / magenta / white); auto-spawn fills toward half in tens of seconds and auto-drain keeps it from packing solid
- [ ] 8.3 Colour buttons dump their primary at random X; drain button empties the scene then spawning resumes; display stays stable (`service()` not starved)
