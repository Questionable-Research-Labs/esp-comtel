# tetris Specification

## Purpose
Define the `tetris` plugin: full-screen upright 16x96 Tetris with three-button controls, serial score and a game-over flash.
## Requirements
### Requirement: Tetris plugin entry point

The system SHALL provide a `tetris` plugin at `src/plugins/tetris.cpp` with the descriptor `// @plugin: Tetris`, following the plugin-system format. Its loop SHALL call `panel.service()` on every pass, and SHALL call `panel.show()` only when the frame has changed (or on a game-over flash toggle).

#### Scenario: Display stays fed
- **WHEN** the game is idle between gravity steps
- **THEN** `panel.service()` still runs every loop pass, and no redundant `show()` is issued

### Requirement: Full-screen upright playfield

The plugin SHALL treat the sign as mounted upright, with a playfield of 16 columns × 96 rows mapped one cell per pixel, filling the whole canvas with no margins or on-glass HUD. Game cell `(gx, gy)`, with `gx` in 0..15 and `gy` in 0..95, SHALL map to canvas pixel `(gy, gx)`.

#### Scenario: Coordinate mapping
- **WHEN** a locked cell exists at game `(3, 10)`
- **THEN** canvas pixel `(10, 3)` is lit

#### Scenario: Line clear width
- **WHEN** all 16 cells of a row are filled
- **THEN** that row is cleared and the rows above shift down

### Requirement: Core Tetris gameplay

The plugin SHALL implement standard Tetris rules ported from espTetris and adapted to 16×96:
- seven tetrominoes with four rotations each
- a 7-bag randomizer
- spawn at the top with `x = 6`
- line-clear scoring of 100 / 300 / 500 / 800 for 1–4 lines
- top-out detection with automatic restart

Gravity SHALL start at 700 ms per row and speed up with score through the tiers 0→700, 500→600, 1500→500, 3000→400, 5000→300, 8000→220, 12000→160, 18000→120, 25000→90 ms. Soft drop SHALL use a 50 ms interval.

#### Scenario: 7-bag
- **WHEN** seven pieces are spawned from a fresh bag
- **THEN** each of the seven tetromino types appears exactly once

#### Scenario: Line clear scoring
- **WHEN** a lock clears two rows
- **THEN** the score increases by 300

#### Scenario: Top-out and restart
- **WHEN** a newly spawned piece collides with locked cells
- **THEN** the game holds in a topped-out state for about one second, then resets the board, score and bag

### Requirement: Three-button controls

The plugin SHALL read three active-low buttons with internal pull-ups and a 20 ms debounce:

| Button | Pin | Action |
|---|---|---|
| left | `BTN_PIN_1` (GPIO 32) | move left |
| right | `BTN_PIN_2` (GPIO 33) | move right |
| rotate | `BTN_PIN_EXTRA` (GPIO 2) | rotate clockwise on release if held less than 500 ms; held 500 ms or longer means soft drop while held, with no rotation on release |

Left and right SHALL move once on press, then auto-repeat (DAS) every 80 ms after being held for 200 ms. Input SHALL be ignored until all three buttons have been seen released once after boot.

#### Scenario: Short press rotates
- **WHEN** rotate is pressed and released within 500 ms
- **THEN** the piece rotates clockwise once, on release

#### Scenario: Long press soft-drops
- **WHEN** rotate is held for 500 ms or more
- **THEN** the piece falls at the soft-drop rate while held, and does not rotate on release

#### Scenario: DAS
- **WHEN** left is held for 200 ms or more
- **THEN** the piece keeps moving left every 80 ms until released

#### Scenario: Input arming
- **WHEN** a button is held during boot
- **THEN** no action fires until every button has been released once

### Requirement: Monochrome rendering

The plugin SHALL draw every locked cell and the active piece in `COMTEL_WHITE` on a `COMTEL_OFF` background. There SHALL be no per-piece colors or patterns.

#### Scenario: All pieces look the same
- **WHEN** any piece type is on the board
- **THEN** its cells are `COMTEL_WHITE`

### Requirement: Serial score reporting

The plugin SHALL open serial at 115200 baud, print the score whenever it changes, and print the final score at game over. The score SHALL NOT be shown on the sign.

#### Scenario: Score on game over
- **WHEN** the game tops out
- **THEN** `game over — score: <n>` is printed before the restart

### Requirement: Game-over flash

During the topped-out hold, the plugin SHALL invert every playfield pixel about every 150 ms, alternating between white and off, until the game resets. Normal rendering SHALL resume when it does.

#### Scenario: Flash during hold
- **WHEN** the game is topped out
- **THEN** the playfield alternates between normal and inverted about every 150 ms until the restart

