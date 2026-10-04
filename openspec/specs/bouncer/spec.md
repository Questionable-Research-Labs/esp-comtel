# bouncer Specification

## Purpose

Provide a bouncing-square animation renderer for the LED panel.

## Requirements

### Requirement: Bouncing square animation

The system SHALL provide a `Bouncer` renderer that draws a 2x2-pixel square moving on a 45° diagonal across the 96x16 panel, drawn to `panel.canvas` and pushed via `panel.show()`, advancing one step each time a step interval elapses while running.

#### Scenario: Random start on first run
- **WHEN** the Bouncer is started (running turned on) with no prior position
- **THEN** the square's top-left origin is placed at a random position fully on-screen (x in 0..W-2, y in 0..H-2)
- **AND** its initial direction is a 45° diagonal (dx and dy each -1 or +1)

#### Scenario: Diagonal motion
- **WHEN** a step interval elapses while running
- **THEN** the square's origin moves by dx on the x axis and dy on the y axis (one pixel each)
- **AND** the whole 2x2 square remains within the panel bounds after the step

#### Scenario: Bounce off a vertical wall
- **WHEN** advancing would move any part of the square past the left edge (x < 0) or right edge (x+2 > W)
- **THEN** dx is negated and the square stays within bounds, reflecting its horizontal direction

#### Scenario: Bounce off a horizontal wall
- **WHEN** advancing would move any part of the square past the top edge (y < 0) or bottom edge (y+2 > H)
- **THEN** dy is negated and the square stays within bounds, reflecting its vertical direction

#### Scenario: Corner bounce
- **WHEN** advancing would exceed bounds on both axes in the same step
- **THEN** both dx and dy are negated and the square reverses along both axes

#### Scenario: Paused renderer does not advance
- **WHEN** the Bouncer is not running
- **THEN** `tick()` neither moves the square nor calls `panel.show()`
