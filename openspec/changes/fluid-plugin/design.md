## Context

The Comtel firmware is plugin-per-image: one `src/plugins/<id>.cpp` compiled with the shared `ComtelPanel` driver. The canvas is 96×16 landscape, 3-bit RGB on/off (`COMTEL_RED` / `GREEN` / `BLUE` and the additive mixes). Four internal-pull-up buttons are already named in `button_pins.h` and used together by `entanglement` (GPIO 32, 33, 2, 15).

This change adds an attract-mode fluid: particles fall on the **landscape** panel (unlike `tetris`, which remaps to an upright 16×96 playfield). The hardware constraints that matter are the 16-pixel height, the on/off-per-channel palette, and the need to call `panel.service()` often so the multiplex does not freeze.

## Goals / Non-Goals

**Goals:**

- Deliver a flashable `fluid` plugin that reads as a simple liquid on the 96×16 glass.
- Drop random blocks of the three primaries from the top; mix additively where they overlap.
- Auto-spawn at a rate that would fill the panel in about a minute; auto-drain from ~half full so it never packs solid.
- Map three buttons to the three primaries and the fourth to emptying the scene.
- Leave `ComtelPanel` and the driver core unchanged.

**Non-Goals:**

- Continuum CFD (Navier–Stokes, SPH, pressure solvers).
- Per-channel brightness / gradients (palette is on/off).
- Start / win screens, scoring, or serial UI beyond optional debug prints.
- Sharing sim code with other plugins, or a multi-file plugin.
- New wiring or extra buttons beyond the existing four internal-pull-up pins.
- Changing plugin discovery or the flasher.

## Decisions

### Decision 1: Single self-contained plugin file

All occupancy, stepping, spawning, draining, input, and drawing live in `src/plugins/fluid.cpp`, matching the plugin-system convention. File-local `static` state and helpers (or a `namespace fluid`) keep sections readable without extra `.cpp` files.

*Alternative — `plugins/fluid/` folder:* rejected; discovery treats every `.cpp` as a plugin and the build filter is one file.

### Decision 2: Landscape gravity, one cell per pixel

Logical grid is `WIDTH = 96`, `HEIGHT = 16`, indexed as `grid[y][x]`, drawn with `canvas.drawPixel(x, y, color)`. Floor is `y = 15`. No canvas rotation.

*Alternative — upright like tetris:* rejected; the request is horizontal format. The interesting axis is the 96-wide pool, not a 96-deep column.

### Decision 3: Per-channel occupancy (additive mix)

Store one `uint8_t` per cell, low 3 bits = R/G/B occupancy (`COMTEL_*` values directly). A particle is one set bit. Different primaries may share a cell; the pixel colour is the OR of those bits. Same primary cannot stack in one cell.

This is the mixing model the panel is built for: red sand and green sand in one LED become yellow, all three become white.

*Alternative — one particle per cell, no overlap:* rejected; colours would only sit side-by-side and would not mix.

*Alternative — blend on collision into a single secondary particle:* rejected; it loses mass and cannot make white from three primaries cleanly.

### Decision 4: Falling-sand liquid CA, not a grid fluid solver

Each sim step, for each occupied channel bit, try moves in order: **down**, **down-left / down-right** (random order), **left / right** (random order). A move is legal if the destination is in bounds and that channel bit is clear. On a successful move, clear the bit in the source and set it in the destination (other channels in both cells are untouched).

Scan **bottom-up** (then a consistent left-to-right or randomized X) so a particle that already fell this step is not processed again.

Horizontal slides make a 16-px-tall column actually look like a pool instead of 96 independent sand stacks. Cap at one cell per step so it does not teleport.

*Alternative — Navier–Stokes / SPH:* rejected. 1536 cells on an ESP32 that must multiplex the panel every loop; the request is a simple fluid.

*Alternative — sand-only (no sideways slide):* rejected; blobs would pile in columns and barely mix.

### Decision 5: Spawn blocks and fill rate

A drop is a compact rectangle, **4×3 cells** (12 particles of one primary), clipped to stay on-canvas, top-aligned at `y = 0`. Auto-spawn picks a random primary and a random X. Colour buttons spawn the same shape of their own primary.

Fill rate with drain disabled: 1536 cells / ~60 s ≈ 26 particles/s. At 12 particles/block that is an auto-drop about every **450 ms**. Treat 4×3 and 450 ms as starting constants; tune on hardware so a no-drain run fills in about a minute.

Button drops are extra volume on top of auto-spawn.

*Alternative — only button drops:* rejected; the minute-to-fill rate would depend on mashing buttons.

*Alternative — single pixels:* rejected; “a block of particles” should read as a blob hitting the pool.

### Decision 6: Conveyor auto-drain from half full

Fill = count of cells with `grid[y][x] != 0`. Threshold = `WIDTH * HEIGHT / 2` = 768.

While fill ≥ 768, on a slower cadence than gravity, **shift the whole grid down one row** and clear row 0 (the former bottom row is discarded). That matches “drop the bottom row and shift everything down.” Gravity then continues on the new layout.

Choose the drain cadence so that, with auto-spawn running, fill hovers near half instead of climbing. A packed row is 96 occupied cells; at ~26 particles/s, removing one row every ~3–4 s roughly balances inflow. Start there and tune.

Do **not** shift every gravity step — that would empty the panel in 16 frames.

*Alternative — only clear the bottom row and wait for gravity:* closer to a floor drain, but slower and less like the requested shift. Conveyor is what was asked.

### Decision 7: Drain button empties the scene

GPIO 15 (`BTN_PIN_EXTRA_2`) on press edge starts a **scene drain**: set a flag, pause auto-spawn, ignore colour drops, and shift down every sim step (or every few frames) until fill is 0. Then clear the flag and resume auto-spawn.

Faster than auto-drain, one press, uses the same shift primitive. Colour buttons: 32 red, 33 green, GPIO 2 blue — same four pins as `entanglement`, 20 ms debounce, boot arming until all released.

*Alternative — hold-to-drain:* also fine, but “drains the scene” reads as a one-shot empty.

*Alternative — instant `memset` clear:* cheaper, but skips the visual of the pool dropping out; the shift is the point.

### Decision 8: Timing vs multiplex

Target a sim step around **40 ms**. Each `loop()`: `panel.service()` always; when the step interval elapses, step physics / spawn / drain, rebuild the canvas from the grid, `panel.show()`. `show()` is ~12 ms; leftover time is scanning.

Rebuild is `fillScreen(COMTEL_OFF)` then `drawPixel` for every occupied cell (or walk the 1536 cells and write the byte). 1536 pixels is cheap.

### Decision 9: Build and docs

`// @plugin: Fluid` plus `python tools/flash.py --sync` generates `[env:fluid]`. README lists the plugin and the four-button map. No `docs/wiring.md` change — those pins are already documented.

## Risks / Trade-offs

- **[Risk] `service()` starvation if a step does too much work** → Mitigation: 1536 cells × 3 bits max, one pass per step; no allocations; keep helpers trivial. If the panel flickers, split the pass or lower the step rate.
- **[Risk] Fill-rate / drain-rate feel wrong on glass** → Mitigation: named constants for block size, spawn interval, auto-drain interval, scene-drain interval; tune in one place after first flash.
- **[Risk] GPIO 2 and 15 are strapping pins** → Mitigation: same as `entanglement` / `tetris`: internal pull-up, idle high, must not be held low at reset. Document in the plugin header.
- **[Risk] Sideways slides look more like gas than liquid** → Mitigation: only slide when down and diagonals are blocked; one cell per step. If it spreads too thin, drop the horizontal move.
- **[Risk] Mixing is hard to see if blobs never overlap** → Mitigation: per-channel co-occupancy plus a 96-wide pool; auto-spawn of random primaries plus button dumps. If overlap is rare, enlarge the block or raise spawn rate slightly.

## Migration Plan

1. Add `src/plugins/fluid.cpp`.
2. Run `python tools/flash.py --sync`.
3. Compile-check with `python tools/flash.py fluid --no-upload` (or `pio run -e fluid`).
4. Flash and tune spawn/drain constants on the sign.
5. Rollback is “don’t flash `fluid`” — no driver or other-plugin changes.

## Open Questions

None blocking. Assumptions to revisit only if the glass disagrees:

- 4×3 block and ~450 ms auto-spawn as the minute-to-fill starting point.
- Scene drain is a one-press empty (not hold-to-drain).
- Auto-spawn of random primaries runs in addition to the colour buttons.
