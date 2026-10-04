## 1. Remove Cursor tooling

- [x] 1.1 Delete `.cursor/` (contents verified identical to `.claude/` below the frontmatter; nothing to integrate)

## 2. Retire the in-flight changes

- [x] 2.1 `more-colors`: tick tasks 1.1–3.1 and 4.1 (verified implemented in `comtel_panel.*`, all plugins and `comtel_diag.cpp`); leave 4.2 (on-glass colour bars) unticked, carried as 6.1 below
- [x] 2.2 `tetris-plugin`: leave 7.1–7.3 unticked, carried as 6.2 below
- [x] 2.3 `text-input`: leave 4.2–4.5 unticked, carried as 6.3 below
- [x] 2.4 Archive all three with `openspec archive <name> --skip-specs -y` (their specs are superseded by this change's corrected `comtel-panel`, `tetris` and `scrolling-text` specs)
- [x] 2.5 Confirm `openspec list` shows only `tidy-up` as active

## 3. Project context

- [x] 3.1 Fill `openspec/config.yaml` `context:` with the hardware (ESP32 esp32dev + Comtel 96×16 RGB sign via 74HCT245), build (PlatformIO, Arduino, one env per plugin plus `diag`, `tools/flash.py`), plugin convention (`src/plugins/<id>.cpp`, `// @plugin:` label, depends only on `src/driver/`), docs locations (`docs/PROTOCOL.md`, `docs/wiring.md`), and the rule that specs follow the code

## 4. Documentation drift

- [x] 4.1 `README.md`: fix the `!shift` default (1 → 0)
- [x] 4.2 `README.md`: replace "current plugins render monochrome white… Colour canvas support is the obvious next upgrade" with a short note on the `COMTEL_*` palette
- [x] 4.3 `README.md`: add `button_pins.h`, `tetris`, `entanglement`, `button_test` and `row_uniformity` to the code-layout table
- [x] 4.4 `src/plugins/entanglement.cpp`: comments only. Change "nine electrons" / "all 9" / "all-nine" to match `N_ELECTRONS = 15` and `WIN_ELECTRONS = 8` (win is a strict majority, not holding all of them). No code changes.
- [ ] 4.5 Update the `bouncer` spec's Purpose line at sync time (from "renderer" to "minimal reference plugin"), since a delta cannot change Purpose

## 5. Validate

- [x] 5.1 `openspec validate tidy-up --strict` passes
- [x] 5.2 All envs still compile: `pio run -e bouncer -e scrolling_text -e tetris -e entanglement -e button_test -e row_uniformity -e diag`
- [x] 5.3 Commit on `web-launcher` (including `openspec/`, now tracked)

## 6. Hardware verification (needs the sign; carried over from archived changes)

- [ ] 6.1 Colour: draw R, G, B, Y, C, M, W bars and confirm each channel mapping (from `more-colors` 4.2)
- [ ] 6.2 Tetris: uploads and runs upright; left/right/rotate, long-press soft drop, DAS, line clears with serial score, and the top-out flash and auto-restart with no display starvation (from `tetris-plugin` 7.1–7.3)
- [ ] 6.3 Scrolling text: typed text scrolls; `!size 1/2`, `!bold`, `!italic` alone and combined wrap cleanly; settings survive a power cycle; first boot after `pio run -t erase` shows the defaults (from `text-input` 4.2–4.5)

## 7. Close out

- [ ] 7.1 Archive `tidy-up` (syncs specs into `openspec/specs/`). If the sign is still unavailable, the user decides whether to defer section 6 to a follow-up checklist and archive anyway (design Open Questions)
