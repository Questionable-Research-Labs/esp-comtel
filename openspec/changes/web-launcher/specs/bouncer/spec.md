## MODIFIED Requirements

### Requirement: Bouncing square animation

The system SHALL provide a `bouncer` plugin at `src/plugins/bouncer.cpp`, registered with the plugin host, that draws a 2×2 square in `COMTEL_WHITE` on `COMTEL_OFF`, moving on a 45° diagonal across the 96×16 panel. Each `tick()` SHALL step the square if 40 ms have passed since the last step, then call `panel.show()`. It is the minimal reference plugin: it has no settings, serial interface or buttons. The animation logic SHALL be file-local.

#### Scenario: Random start on every start
- **WHEN** the plugin is started (at boot or by switching to it)
- **THEN** the square's top-left corner is placed at a random position fully on-screen (x in 0..W-2, y in 0..H-2)
- **AND** its direction is a random 45° diagonal (dx and dy each -1 or +1)

#### Scenario: Diagonal motion
- **WHEN** 40 ms have elapsed since the last step
- **THEN** the square moves one pixel by dx and one pixel by dy, and stays fully within the panel

#### Scenario: Bounce off a vertical wall
- **WHEN** the next step would put any part of the square past the left edge (x < 0) or the right edge (x + 2 > W)
- **THEN** dx is negated before the step, and the square stays within bounds

#### Scenario: Bounce off a horizontal wall
- **WHEN** the next step would put any part of the square past the top edge (y < 0) or the bottom edge (y + 2 > H)
- **THEN** dy is negated before the step, and the square stays within bounds

#### Scenario: Corner bounce
- **WHEN** the next step would exceed bounds on both axes
- **THEN** both dx and dy are negated
