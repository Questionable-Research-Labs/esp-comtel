## Why

The OpenSpec record has drifted from the code. `openspec/` was gitignored until now, so the specs never travelled with the repo: three changes (`more-colors`, `tetris-plugin`, `text-input`) are implemented but were never archived, the `bouncer` spec still describes a pausable renderer that no longer exists, and the driver, button pins and most plugins have no spec at all. Before the next real piece of work (the web launcher, which rewrites the plugin model), the specs need to describe what the code actually does today.

## What Changes

- **Remove `.cursor/`.** It holds only OpenSpec commands and skills, byte-identical (below the frontmatter) to `.claude/`. Nothing to integrate.
- **Fold the three in-flight changes into this one and archive them.** Their delta specs are corrected against the current code and re-homed here; the changes are then archived with `--skip-specs` so their stale deltas are not applied. Their outstanding on-hardware verification tasks carry over into this change's tasks.
- **Consolidate `text-input`'s three capabilities** (`serial-console`, `settings-persistence`, `text-styling`) into a single per-plugin `scrolling-text` spec, with paths corrected from the pre-plugin layout (`comtel_main.cpp`, `scroll_text.*`) to `src/plugins/scrolling_text.cpp`.
- **Write specs from the code** for the driver (canvas, palette, show/service contract, burst calibration and duty), the reserved button pins, and the `entanglement` plugin.
- **Rewrite the `bouncer` spec** to match the minimal standalone plugin (no `Bouncer` class, no pause/run state).
- **Bring `plugin-system` up to date**: plugins may depend on the shared `button_pins.h` and include plugin-private data headers (e.g. screen images).
- **Fill in `openspec/config.yaml` project context** so future changes start with the hardware, layout and conventions.
- **Fix small doc drift found on the way**: the README's `!shift` default (code is 0, README says 1), its claim that plugins are monochrome and colour is "the next upgrade" (the colour canvas has shipped), its code-layout table (missing `button_pins.h`, `entanglement`, `button_test`, `row_uniformity`), and `entanglement.cpp` comments that still say "nine electrons" (code: 15, win at 8).

No firmware behavior changes. Diagnostic plugins `button_test` and `row_uniformity` are intentionally left unspecced.

## Capabilities

### New Capabilities
- `comtel-panel`: the `ComtelPanel` driver contract — 96×16 `GFXcanvas8` canvas, 3-bit `COMTEL_*` palette, per-channel subpixel mapping, `show()`/`service()` contract, burst calibration and equal-duty scanning.
- `button-pins`: the shared declaration-only header of GPIOs reserved for plugin buttons, disjoint from the driver's pins.
- `scrolling-text`: the scrolling-message plugin — serial console, playlist, text size/bold/italic, buttons, NVS persistence (absorbs `text-input`'s three specs).
- `tetris`: the upright 16×96 Tetris plugin (from `tetris-plugin`, corrected to the palette and shared pin names).
- `entanglement`: the two-player, four-button colour/electron game with start, play and win screens.

### Modified Capabilities
- `bouncer`: requirements rewritten — the bouncer is a minimal standalone plugin, not a pausable `Bouncer` renderer.
- `plugin-system`: shared core explicitly includes `button_pins.h`; plugins may include plugin-private data headers.

## Impact

- **OpenSpec:** `openspec/specs/` gains five specs and two are rewritten; `openspec/changes/{more-colors,tetris-plugin,text-input}` move to `openspec/changes/archive/`; `openspec/config.yaml` gains context.
- **Repo:** `.cursor/` deleted (already in `.gitignore`).
- **Docs/comments:** `README.md` (`!shift` default), `src/plugins/entanglement.cpp` comments only.
- **Firmware:** none. No source behavior changes; all environments should still build.
