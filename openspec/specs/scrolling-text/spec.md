# scrolling-text Specification

## Purpose
Define the `scrolling_text` plugin: a four-slot scrolling-message playlist with size/bold/italic styling, a serial console, two-button control and NVS persistence.
## Requirements
### Requirement: Scrolling message plugin

The system SHALL provide a `scrolling_text` plugin at `src/plugins/scrolling_text.cpp` that scrolls a text message right-to-left across the sign in `COMTEL_WHITE` on `COMTEL_OFF`, advancing one pixel per step interval. A three-space gap SHALL be appended after the message before it repeats. Once the message, including bold and italic extra width, has fully left the left edge, it SHALL restart from the right edge. The renderer SHALL be file-local to the plugin.

#### Scenario: Message scrolls and repeats
- **WHEN** a message is set
- **THEN** it enters from the right edge, scrolls left one pixel per step, and after fully exiting re-enters from the right

### Requirement: Four-slot playlist

The plugin SHALL keep a playlist of four message slots of up to 127 characters each. Slot 0 SHALL default to `QUESTIONABLE RESEARCH`, slot 1 to `HELLO`, and slots 2 and 3 SHALL default to empty. One slot is current at a time. Advancing to the next message SHALL skip empty slots.

#### Scenario: Next message skips empties
- **WHEN** the current slot is 1 and slots 2 and 3 are empty
- **THEN** advancing to the next message shows slot 0

### Requirement: Text size selection

The plugin SHALL support size 1 (6×8 glyphs, drawn at cursor y = 4, so vertically centered) and size 2 (12×16 glyphs filling the panel height), defaulting to size 2. Setting any value other than 1 or 2 SHALL be ignored. Changing size SHALL restart the scroll from the right edge.

#### Scenario: Small text is centered
- **WHEN** size 1 is selected
- **THEN** glyphs render 6×8 with four blank rows above and below

#### Scenario: Invalid size ignored
- **WHEN** size 5 is requested
- **THEN** the size is unchanged

### Requirement: Bold styling

The plugin SHALL support bold at both sizes as a double-strike: the message is drawn a second time, offset right by `size` pixels. Bold SHALL default off, SHALL combine with italic, and SHALL add `size` pixels to the scrolled width. Toggling bold SHALL restart the scroll.

#### Scenario: Bold thickens strokes
- **WHEN** bold is on at size 2
- **THEN** each stroke is widened by 2 px, and the scroll wraps only after the widened text has fully exited

### Requirement: Italic styling

The plugin SHALL support italic at both sizes as a row shear applied after drawing. Each canvas row `y` SHALL be shifted right by `(15 - y) / 4` pixels, so the bottom row stays fixed and the top leans 3 px right. Colors SHALL be preserved, and vacated columns SHALL be filled with `COMTEL_OFF`. Italic SHALL default off, SHALL combine with bold, and SHALL add up to 3 px to the scrolled width. Toggling italic SHALL restart the scroll.

#### Scenario: Italic leans the text
- **WHEN** italic is on
- **THEN** rows lean progressively right toward the top, with the bottom row unshifted

### Requirement: Serial console

The plugin SHALL accept line-based input on serial at 115200 baud: CR is ignored, LF ends a line, leading spaces are trimmed, and lines are capped at 129 characters. A non-empty line not starting with `!` SHALL replace the current slot's message, show it immediately, and echo the slot and text back. Empty or whitespace-only lines SHALL be ignored silently. These commands SHALL be supported:

| Command | Effect |
|---|---|
| `!slot <n>` | switch to slot n (clamped to 0..3) |
| `!list` | print all slots, marking the current one |
| `!size <1\|2>` | text size |
| `!bold [0\|1]`, `!italic [0\|1]` | with no argument, toggle; with an argument, set |
| `!speed <ms>` | step interval, clamped to 5..2000 |
| `!shift <n>` | driver `rowShiftLo = n & 7` (session only) |
| `!card` | module test card: six vertical lines at x = 17m; pauses scrolling |
| `!stair` | row-mapping staircase (canvas row y lit at columns 6y..6y+5); pauses scrolling |
| `!row <y>` | light only canvas row `y & 15`; pauses scrolling |
| `!stop` / `!go` | pause / resume scrolling |
| `!status` | print slot, message, speed, running, size, bold, italic and calibration |
| anything else starting with `!` | print help |

Commands that change state SHALL print the status line afterward.

#### Scenario: Typing a message
- **WHEN** the user sends `HELLO WORLD`
- **THEN** the current slot becomes `HELLO WORLD`, it starts scrolling, and the console echoes `slot <n> set: "HELLO WORLD"`

#### Scenario: Toggling bold
- **WHEN** the user sends `!bold` while bold is off
- **THEN** bold turns on, and `!status` reports `bold=1`

#### Scenario: Speed clamped
- **WHEN** the user sends `!speed 1`
- **THEN** the step interval becomes 5 ms

#### Scenario: Unknown command
- **WHEN** the user sends `!foo`
- **THEN** the help text is printed

### Requirement: Two-button control

The plugin SHALL read BTN1 (`BTN_PIN_1`, GPIO 32) and BTN2 (`BTN_PIN_2`, GPIO 33) with internal pull-ups and a 30 ms debounce. A press SHALL count as long when held for 600 ms; the long action SHALL fire while the button is still held, and its release SHALL NOT also trigger the short action.

| Button | Short press | Long press |
|---|---|---|
| BTN1 | next non-empty message | pause / resume |
| BTN2 | cycle speed preset 90 → 45 → 20 ms (slow, medium, fast) | test card |

#### Scenario: Speed cycle
- **WHEN** BTN2 is short-pressed at the medium speed (45 ms)
- **THEN** the speed becomes 20 ms

#### Scenario: Long press does not double-fire
- **WHEN** BTN1 is held for one second and released
- **THEN** scrolling toggles pause once, and the message does not advance

### Requirement: Settings persist to NVS

The plugin SHALL save all four playlist slots (`msg0`..`msg3`), the current slot (`slot`), size (`size`), bold (`bold`), italic (`ital`) and step interval (`speed`) to NVS Preferences namespace `ledsign` after every user change made by text line, command, or button. At boot, before the first message is shown, it SHALL restore them. Missing keys SHALL fall back to the compiled defaults: the default playlist, slot 0, size 2, bold and italic off, 45 ms. If the restored slot is empty, slot 0 SHALL be shown.

#### Scenario: Power cycle keeps state
- **WHEN** the user sets a message, `!size 1`, `!bold` and `!speed 30`, then power-cycles the sign
- **THEN** it boots scrolling that message, small and bold, at 30 ms

#### Scenario: First boot
- **WHEN** no `ledsign` NVS data exists
- **THEN** the sign scrolls `QUESTIONABLE RESEARCH` at size 2, with no styles, at 45 ms

#### Scenario: Empty restored slot
- **WHEN** the restored current slot is empty
- **THEN** slot 0 is shown

### Requirement: Calibration is not persisted

The `!shift` value and the driver clock calibration SHALL NOT be saved to or restored from NVS; after a reboot they SHALL return to the driver defaults (`rowShiftLo = 0`).

#### Scenario: Shift resets on reboot
- **WHEN** the user sends `!shift 3` and reboots
- **THEN** `rowShiftLo` is 0

