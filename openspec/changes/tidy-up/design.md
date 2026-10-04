## Context

`openspec/` was gitignored, so the spec record lived only on one machine while the code moved on in the shared repo (`Questionable-Research-Labs/esp-comtel`). The state at the start of this change:

```
openspec/specs/            vs. code
├── plugin-system   mostly accurate (no mention of button_pins.h / private data headers)
├── plugin-flasher  accurate
└── bouncer         stale: describes a pausable Bouncer renderer; code is a minimal plugin

openspec/changes/ (in flight, never archived)
├── more-colors    0/10 ticked  -> fully implemented (GFXcanvas8, COMTEL_* palette)
├── tetris-plugin 16/19         -> only on-hardware verification open
└── text-input    12/16         -> only on-hardware verification open; written against
                                   the pre-plugin layout (comtel_main.cpp, scroll_text.*)

unspecced: driver burst/duty calibration, button_pins.h, entanglement
           (and the diagnostic plugins button_test, row_uniformity -- out of scope)
.cursor/   identical copy of .claude/ OpenSpec tooling
```

This change is documentation-only: no firmware behavior changes. It sets a clean baseline before the web-launcher work, which will substantially modify `plugin-system` and `plugin-flasher`.

## Goals / Non-Goals

**Goals:**
- `openspec/specs/` describes the code as it is today, one spec per plugin plus the shared core.
- No in-flight changes remain whose work is already done.
- The open on-hardware verifications are not lost.
- Future changes get project context from `openspec/config.yaml`.

**Non-Goals:**
- Changing firmware behavior, even where the code looks odd. Specs record what the code does; questionable behavior is noted, not fixed.
- Specs for the diagnostic plugins `button_test` and `row_uniformity`.
- Specs for `diag` (`comtel_diag.cpp`) — frozen archaeology firmware.
- Restructuring `docs/` (PROTOCOL.md, wiring.md stay the hardware reference).

## Decisions

### 1. Code is the source of truth; specs are reverse-engineered from it

Where an old spec, a comment, the README and the code disagree, the spec follows the code. Examples found:

| Topic | Old record | Code | Spec says |
|---|---|---|---|
| `rowShiftLo` default | text-input spec + README: 1 | `comtel_panel.h`: 0 | 0 |
| Tetris pixel value | "canvas value 1" | `COMTEL_WHITE` | `COMTEL_WHITE` on `COMTEL_OFF` |
| Entanglement electrons | comments: nine, win at nine | `N_ELECTRONS = 15`, `WIN_ELECTRONS = 8` | 15, win at 8 |
| `show()` duration | header comment "~12 ms" | depends on calibration | not specced as a number |

Comments and README that contradict the code are fixed in this change (they're docs); code is not touched.

*Alternative — spec the intended behavior and file bugs:* rejected; nobody has stated intent beyond the code, and a baseline that doesn't match the firmware is worse than none.

### 2. Fold in-flight changes, archive them with `--skip-specs`

The three changes' delta specs are partly wrong (old paths, 1 bpp values, `!shift` default). Rather than syncing them and then correcting, their content is rewritten into this change's specs, and each old change is archived with `openspec archive <name> --skip-specs` so its stale delta never touches `openspec/specs/`. Their tasks are ticked to reflect what the code shows is done, and the outstanding on-hardware checks are copied into this change's task list so archiving doesn't drop them.

Order matters: archive the three old changes **before** archiving `tidy-up`, so `tidy-up`'s ADDED `comtel-panel`/`tetris`/`scrolling-text` specs land on a clean `openspec/specs/`.

*Alternative — archive each with spec sync, then patch:* rejected; it writes known-wrong specs into the baseline and produces three extra rounds of MODIFIED deltas.

*Alternative — leave them open until hardware verification:* rejected by the user; the code is merged and in use, and hardware checks are tracked here instead.

### 3. One spec per plugin; `text-input`'s three specs merge into `scrolling-text`

`serial-console`, `settings-persistence` and `text-styling` only ever applied to the scrolling-text app. Under the plugin model each plugin is self-contained, so its spec should be too. Capabilities: `comtel-panel`, `button-pins`, `plugin-system`, `plugin-flasher`, and one spec per specced plugin (`bouncer`, `scrolling-text`, `tetris`, `entanglement`).

This also lines up with the web launcher, where per-plugin settings (the scrolling-text message) will extend that plugin's spec.

### 4. Driver spec covers the contract and the duty mechanism, not timings

`comtel-panel` specs the plugin-facing contract (canvas, palette, `show()`/`service()`) and the equal-duty behavior (rolling scan, scan-during-burst, ADDR = 0 at latch), plus the calibration fields and their defaults. It does not spec microsecond timings beyond defaults: those are tuning values, and `docs/PROTOCOL.md` remains the protocol reference.

### 5. `bouncer` spec: one MODIFIED requirement

The existing requirement name ("Bouncing square animation") is kept and its full body replaced, dropping the `Bouncer` class and the paused-renderer scenario. Keeping the name makes it a MODIFIED rather than a REMOVED+ADDED pair, which archives cleanly.

### 6. `.cursor/` is deleted outright

Diffed every file below the frontmatter against `.claude/`: identical. Nothing to integrate. It is already listed in `.gitignore`.

### 7. `config.yaml` context: short and factual

Hardware, build system, plugin convention, where protocol docs live, and the "spec follows code" rule. No style rules invented on the user's behalf.

## Risks / Trade-offs

- **[Reverse-engineered specs encode accidental behavior as requirements]** → Acceptable for a baseline; Decision 1 makes this explicit. Future changes can MODIFY.
- **[Entanglement spec is large and closely tied to tuning constants]** → Spec the rules and states; mention tuning constants only where they define a rule (electron count, win threshold, timeouts), not every physics knob.
- **[Unverified-on-hardware work gets archived]** → The hardware checks move into this change's tasks (section "Hardware verification") and stay open until the sign is back on the bench. `tidy-up` itself isn't archived until those are done or explicitly deferred.
- **[Someone else's local openspec work never surfaced]** → User confirmed this doesn't matter; the specs are rebuilt from code.

## Migration Plan

1. Delete `.cursor/`.
2. Tick done tasks in `more-colors`, `tetris-plugin`, `text-input`; archive each with `--skip-specs`.
3. Write `openspec/config.yaml` context.
4. Fix README and `entanglement.cpp` comment drift.
5. `openspec validate tidy-up`; compile all envs to confirm docs edits broke nothing.
6. Hardware verification when the sign is available; then archive `tidy-up` (syncs specs).

Rollback: everything is in git on the `web-launcher` branch; revert the commit.

## Open Questions

- Should `tidy-up` be archived before the hardware checks (moving them to a follow-up), so the web-launcher change can start from synced specs? Default: archive once the non-hardware tasks are done, and record the hardware checks as a short follow-up checklist in the README or a new change.
