## ADDED Requirements

### Requirement: Fluid plugin entry point

The system SHALL provide a `fluid` plugin as `src/plugins/fluid.cpp` with a `// @plugin: Fluid` descriptor comment, defining its own Arduino `setup()` and `loop()`. The plugin SHALL depend only on the shared `ComtelPanel` driver and `button_pins.h`, and SHALL NOT modify the driver core.

#### Scenario: Plugin discovered by convention
- **WHEN** `src/plugins/fluid.cpp` exists
- **THEN** the flasher and PlatformIO sync include `fluid` as an available plugin id
- **AND** building `[env:fluid]` compiles only `src/driver/` and `src/plugins/fluid.cpp`

#### Scenario: Panel service loop
- **WHEN** the fluid plugin `loop()` runs
- **THEN** it calls `panel.service()` on every iteration to keep the display multiplexed
- **AND** it calls `panel.show()` after each simulation step that changes the canvas

### Requirement: Landscape playfield and gravity

The fluid plugin SHALL run in **horizontal (landscape) format** on the native 96×16 canvas: 96 columns (`x` = 0..95) by 16 rows (`y` = 0..15), one simulation cell per canvas pixel. Gravity SHALL pull toward the bottom of the short axis (`y` increasing, floor at `y = 15`). The plugin SHALL NOT rotate the playfield the way `tetris` does.

#### Scenario: Coordinate mapping
- **WHEN** a particle occupies cell `(x, y)`
- **THEN** it is drawn at canvas pixel `(x, y)`
- **AND** it falls toward `y = 15`, not along the long axis

#### Scenario: Full canvas
- **WHEN** the simulation is running
- **THEN** the playfield occupies the entire 96×16 canvas with no HUD, gutters, or letterboxing

### Requirement: Primary colours and additive mixing

Each cell SHALL independently record occupancy of the three primary channels red, green, and blue (at most one particle of a given channel per cell). Particles of different primaries MAY occupy the same cell. The plugin SHALL render a cell as the `COMTEL_*` palette value whose RGB bits match the occupied channels, so overlapping primaries mix additively:

| Occupied channels | Colour |
|---|---|
| none | off |
| R | red |
| G | green |
| B | blue |
| R+G | yellow |
| G+B | cyan |
| R+B | magenta |
| R+G+B | white |

A particle of one primary SHALL treat a cell as blocked only when that same primary already occupies the cell; another primary in the cell SHALL NOT block it.

#### Scenario: Two primaries share a cell
- **WHEN** a red particle and a green particle occupy the same cell
- **THEN** that pixel is drawn yellow (`COMTEL_YELLOW`)

#### Scenario: All three mix
- **WHEN** red, green, and blue occupy the same cell
- **THEN** that pixel is drawn white (`COMTEL_WHITE`)

#### Scenario: Same colour does not stack
- **WHEN** a red particle would move into a cell that already holds red
- **THEN** the move is rejected
- **AND** a green particle MAY still enter that cell

### Requirement: Fluid motion

The plugin SHALL step a discrete particle simulation that behaves like a simple liquid: particles fall, then spill diagonally, then spread sideways, until they rest. Each occupied channel in a cell is its own particle and moves independently under those rules. Left versus right SHALL be randomized so the pool does not bias to one side.

#### Scenario: Fall
- **WHEN** the cell directly below a particle has no occupancy of that particle's channel
- **THEN** the particle moves down one row on that step

#### Scenario: Spill
- **WHEN** the cell below is blocked for that channel, and a diagonal-down neighbour is not
- **THEN** the particle moves into a randomly chosen open diagonal-down cell

#### Scenario: Spread
- **WHEN** down and both diagonals are blocked for that channel, and a sideways neighbour is not
- **THEN** the particle may move left or right into an open cell, with the side chosen at random

#### Scenario: Rest
- **WHEN** down, both diagonals, and both sides are blocked for that channel
- **THEN** the particle stays in place for that step

### Requirement: Random drops from the top

The plugin SHALL add particles by dropping a compact rectangular **block** of one primary colour from the top of the playfield (`y` near 0) at a **random horizontal position** that keeps the block on-canvas. Particles SHALL be written only into cells that do not already hold that primary. Auto-spawn SHALL choose the primary at random from {red, green, blue}.

The auto-spawn volume and rate SHALL be such that, with draining disabled, occupied cells (any channel) would fill the 96×16 canvas in approximately **60 seconds**. Auto-spawn SHALL continue while the simulation is running, except while a scene drain (drain button) is in progress.

#### Scenario: Auto-drop
- **WHEN** the auto-spawn interval elapses and a scene drain is not in progress
- **THEN** a block of a randomly chosen primary appears at a random on-canvas X at the top

#### Scenario: Fill rate
- **WHEN** the plugin runs from an empty canvas with auto-drain and the drain button both inactive
- **THEN** the canvas reaches approximately full occupancy in about one minute

#### Scenario: Partial placement
- **WHEN** some cells of the spawn block already hold that primary
- **THEN** the plugin still places the block's particles into the remaining free cells of the block

### Requirement: Auto-drain at half full

The plugin SHALL measure fill as the fraction of cells with at least one occupied channel. When fill is at or above approximately **half** (about 8 of 16 rows, or ≥ 768 occupied cells), the plugin SHALL start draining: periodically **drop the bottom row** (`y = 15`) and **shift every row above it down by one**, leaving the top row empty. Auto-drain SHALL run at a rate that keeps fill near half so the simulation can continue instead of packing solid. When fill is below half, auto-drain SHALL pause.

#### Scenario: Drain starts at half
- **WHEN** occupied-cell count reaches at least 768
- **THEN** auto-drain becomes active and the bottom row is removed on the drain cadence

#### Scenario: Shift down
- **WHEN** an auto-drain step runs
- **THEN** occupancy that was in row `y` moves to row `y+1` for `y` in 0..14
- **AND** row 0 is empty
- **AND** the former bottom row is gone

#### Scenario: Drain pauses below half
- **WHEN** occupied-cell count is below 768
- **THEN** auto-drain does not remove rows
- **AND** gravity and spawning continue

### Requirement: Four-button controls

The plugin SHALL read four internal-pull-up buttons from `button_pins.h`, with a 20 ms debounce. Input SHALL be ignored until all four buttons have been seen released once after boot. Three buttons each drop a block of a different primary at a random X on the **press edge**. The fourth button **drains the scene**.

| Control | Pin | Action |
|---|---|---|
| Drop red | `BTN_PIN_1` (GPIO 32) | one red block at a random on-canvas X |
| Drop green | `BTN_PIN_2` (GPIO 33) | one green block at a random on-canvas X |
| Drop blue | `BTN_PIN_EXTRA` (GPIO 2) | one blue block at a random on-canvas X |
| Drain scene | `BTN_PIN_EXTRA_2` (GPIO 15) | empty the playfield |

A colour-button drop SHALL be in addition to auto-spawn, not a replacement for it. A drain-button press SHALL start a **scene drain**: auto-spawn pauses, bottom rows are shifted off at a faster cadence than auto-drain until no occupancy remains, then auto-spawn resumes. Colour drops during a scene drain SHALL be ignored.

#### Scenario: Colour press drops that primary
- **WHEN** the red button is pressed after input is armed
- **THEN** a red block appears at the top at a random X
- **AND** green and blue buttons likewise drop only their own primary

#### Scenario: Drain button empties the scene
- **WHEN** the drain button is pressed after input is armed
- **THEN** auto-spawn pauses
- **AND** rows shift off the bottom until the canvas has no occupied cells
- **AND** auto-spawn then resumes

#### Scenario: Held button at boot
- **WHEN** a button is held down while the plugin boots
- **THEN** it does not drop a block or start a scene drain
- **AND** input arms only after every button is released

### Requirement: Colour rendering

The plugin SHALL draw every occupied cell with the matching `COMTEL_*` palette constant on a `COMTEL_OFF` background. The plugin SHALL NOT use brightness gradients (the canvas is 3-bit on/off per channel).

#### Scenario: Frame redraw
- **WHEN** a simulation step moves, spawns, or drains particles
- **THEN** the canvas is cleared and redrawn from occupancy before `panel.show()`

#### Scenario: Empty cell
- **WHEN** a cell holds no channels
- **THEN** it is drawn off
