## ADDED Requirements

### Requirement: Runs under the plugin host

The `scrolling_text` plugin SHALL register with the plugin host. Every `start()` SHALL behave like the former boot: restore the playlist, slot, size, bold, italic and speed from NVS (with the existing defaults and empty-slot fallback), resume scrolling, and reset button state. The host resets the driver calibration on every switch, so a `!shift` value SHALL NOT survive switching away and back. `start()` SHALL NOT call `Serial.begin`, and SHALL NOT print the help text; the host owns Serial setup and the boot banner. It MAY print its status line.

#### Scenario: Message survives a switch
- **WHEN** the user sets a message, switches to `bouncer`, then back
- **THEN** the same message scrolls, restored from NVS

#### Scenario: Shift resets on switch
- **WHEN** the user sends `!shift 3`, switches away and back
- **THEN** `rowShiftLo` is 0

### Requirement: Web-editable settings

The plugin SHALL declare these host settings. Each one SHALL apply the same change as its serial equivalent, including restarting the scroll and saving to NVS.

| Key | Label | Type | Constraint | Serial equivalent |
|---|---|---|---|---|
| `message` | Message | Text | ≤127 chars, non-blank | typed line (edits current slot) |
| `slot` | Slot | Choice | `0\|1\|2\|3` | `!slot` |
| `speed` | Speed (ms/step) | Int | 5..2000 | `!speed` |
| `size` | Size | Choice | `1\|2` | `!size` |
| `bold` | Bold | Bool | — | `!bold 0/1` |
| `italic` | Italic | Bool | — | `!italic 0/1` |

A blank `message` SHALL be rejected, matching the console, which ignores blank lines. Each `get` SHALL report the live value. For `message`, that is the current slot's text without the appended repeat gap.

#### Scenario: Set message from the web
- **WHEN** `message` is set to `HELLO` while slot 2 is current
- **THEN** slot 2 becomes `HELLO`, it scrolls immediately, and it is saved to NVS

#### Scenario: Switching slot updates message
- **WHEN** `slot` is set to `0`
- **THEN** slot 0's message scrolls, and the next state read reports `message` as slot 0's text

#### Scenario: Blank message rejected
- **WHEN** `message` is set to an empty or all-space string
- **THEN** it is rejected, and the current slot is unchanged
