## Why

The sign now has a 3-bit RGB canvas and four wired buttons, but the plugins that use them are games and diagnostics — nothing yet that just plays as a living colour display. A slow, mixing fluid on the landscape 96×16 panel is a natural attract-mode piece: primary colours drop in, blend on the glass, and never quite fill up.

## What Changes

- Add a new **`fluid` plugin** as a single self-contained source file at `src/plugins/fluid.cpp`, following the existing plugin convention (`// @plugin:` descriptor, own `setup()`/`loop()`, depends only on `ComtelPanel` and `button_pins.h`).
- Run a **simple particle fluid** on the panel in **horizontal (landscape) format**: 96 columns wide × 16 rows tall, gravity toward the short edge (canvas `y+`).
- **Spawn** compact blocks of particles from the top at random X positions, at a volume and rate that would fill the panel in about a minute if nothing drained.
- **Three primary colours** (red, green, blue) drop as separate particle species and **mix additively** on the glass (R+G=yellow, G+B=cyan, R+B=magenta, all three=white).
- When the panel is about **halfway full**, automatically **drain** by removing the bottom row so the sim can keep running instead of packing solid.
- **Four buttons:** three drop a block of one primary colour at a random X; the fourth drains the scene.
- **Build integration:** `python tools/flash.py --sync` adds `[env:fluid]`. No driver-core changes.

## Capabilities

### New Capabilities

- `fluid`: landscape particle-fluid plugin for the Comtel 96×16 LED sign — primary-colour drops, additive mixing, auto-spawn and auto-drain, four-button control.

### Modified Capabilities

_(none — the new plugin fits the existing plugin-system rules without changing them)_

## Impact

- **New code:** `src/plugins/fluid.cpp` (sim, input, and rendering in one file).
- **Build:** machine-generated `[env:fluid]` block in `platformio.ini` after `--sync`; no new libraries.
- **Docs:** `README.md` — list the plugin and its button map.
- **Unchanged:** `ComtelPanel` driver, `button_pins.h`, other plugins, `diag`, and flasher discovery behaviour (automatic once the `.cpp` exists).
- **Hardware:** uses the same four internal-pull-up buttons as `entanglement` (GPIO 32, 33, 2, 15). No new wiring.
