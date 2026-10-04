# entanglement Specification

## Purpose
Define the `entanglement` plugin: a two-player, four-button game of coloured cubes, electrons, entanglement and stealing, with start, play and win screens.
## Requirements
### Requirement: Entanglement plugin and game modes

The system SHALL provide an `entanglement` plugin at `src/plugins/entanglement.cpp` with the descriptor `// @plugin: Entanglement`: a two-player game on the 96×16 sign. The plugin SHALL have three modes: START, PLAY and WIN. It SHALL boot into START, and the only way into PLAY SHALL be from START, through one game-building path, so a restarted game is identical to a cold boot. START and WIN SHALL each push their image once (by copying a 96×16 byte image into the canvas buffer and calling `show()`) and then do no per-frame drawing; `service()` alone keeps the image lit. The images SHALL live in the plugin-private data headers `start_screen_image.h` and `win_screen_image.h`.

```
          any press                     a player holds WIN_ELECTRONS
 START ──────────────▶ PLAY ─────────────────────────────▶ WIN
   ▲                    │ 30 s with no press                 │ press after 2 s lockout,
   └────────────────────┘                                    │ or 7 s with no press
   ▲─────────────────────────────────────────────────────────┘
```

#### Scenario: Boot to start screen
- **WHEN** the plugin starts
- **THEN** the start image is shown and no game runs until a button is pressed

#### Scenario: Starting press is consumed
- **WHEN** a button is pressed in START
- **THEN** a fresh game begins, and that press does not also recolor or steal

#### Scenario: Idle timeout
- **WHEN** 30 s pass in PLAY without any button press (cube motion does not count)
- **THEN** the game is abandoned and the start image is shown

#### Scenario: Win screen lockout and return
- **WHEN** WIN is entered
- **THEN** all presses are ignored and consumed for 2 s
- **AND** the first press after that returns to START without starting a game
- **AND** with no press, it returns to START 7 s after entering WIN

### Requirement: Four buttons, one job each

The plugin SHALL read four internal-pull-up buttons from `button_pins.h`, with a 20 ms debounce, acting on press edges:

| Player | Color button | Direction / steal button |
|---|---|---|
| 1 | `BTN_PIN_1` (GPIO 32) | `BTN_PIN_EXTRA` (GPIO 2) |
| 2 | `BTN_PIN_2` (GPIO 33) | `BTN_PIN_EXTRA_2` (GPIO 15) |

Input SHALL be ignored until all four buttons have been seen released once after boot. The color button SHALL only recolor its player's cube. The direction button SHALL only change its player's course, or launch a steal while the cubes are entangled. Neither button SHALL affect the opponent's cube.

#### Scenario: Held button at boot
- **WHEN** a button is held down while the plugin boots
- **THEN** it does not start a game, and input arms only after every button is released

### Requirement: Cubes and motion

Each player SHALL control one 3×3 cube that moves with sub-pixel position and velocity, updated every 25 ms frame, and bounces off all four walls. Motion SHALL use discrete headings measured from vertical, {22.5°, 45°, 67.5°, 90°}, in any quadrant, at a random speed of 8–24 px/s. At game start, the cubes SHALL be placed in opposite halves of the panel, each with an independent random color from {red, green, blue} and a random motion. When the cubes overlap, both SHALL get new random motion biased away from each other and be pushed apart along the axis of least overlap, staying fully on-screen.

#### Scenario: Direction press visibly changes course
- **WHEN** a player presses direction while not entangled
- **THEN** their cube gets a new random heading and speed whose heading differs from the current one (the draw is retried up to 8 times; if every retry repeats the heading, the last draw is kept)

#### Scenario: Cube collision
- **WHEN** the two cubes overlap
- **THEN** they are re-randomized away from each other and separated, so the overlap does not repeat on the next frame

### Requirement: Color cycling

Each cube's color SHALL be one of red, green or blue. Player 1's color button SHALL step R → G → B → R. Player 2's SHALL step in the opposite direction, R → B → G → R, so two players pressing at the same rate keep meeting on a shared color.

#### Scenario: Opposite cycles
- **WHEN** both cubes are red and each player presses color once
- **THEN** player 1 is green and player 2 is blue

### Requirement: Electrons, collection and winning

The game SHALL have a fixed set of 15 single-pixel electrons, with no respawn. Each SHALL have a random color from {red, green, blue}, fixed for life. At game start, all of them SHALL float at random cells clear of both cubes' starting boxes and of each other. Each electron SHALL always be in exactly one state:
- **floating:** stationary unless it has just been released
- **orbiting:** in one player's orbit
- **in flight:** moving as a steal

A cube SHALL automatically collect any floating electron under it whose color matches the cube's current color. There is no hold cap. Orbiting electrons SHALL be drawn evenly spaced on the 5×5 ring one pixel outside their owner's cube, rotating slowly (about 2 s per revolution). The first player whose orbit holds 8 electrons SHALL win; the winning frame is not drawn, and the game goes straight to the win image. Because the total is odd and the win threshold is a strict majority, a draw SHALL be impossible.

#### Scenario: Color-matched pickup
- **WHEN** a green cube passes over a floating green electron
- **THEN** the electron joins that player's orbit

#### Scenario: Mismatched electron is ignored
- **WHEN** a green cube passes over a floating red electron
- **THEN** the electron stays floating

#### Scenario: Win
- **WHEN** a capture or pickup brings a player's orbit to 8
- **THEN** the WIN image is shown on that frame

### Requirement: Same-color collision scatters electrons

When the cubes collide while they share a color, every held and in-flight electron SHALL be released. Each one bursts outward from the collision point at about 140 px/s, slows with a per-electron random friction, and comes to rest as a floater. A collision while the cubes have different colors SHALL leave both orbits intact.

#### Scenario: Different-color bump
- **WHEN** a red cube and a blue cube collide
- **THEN** no electrons are released

### Requirement: Entanglement link and pull

While the two cubes share a color, they SHALL be entangled:
- a line in the shared color SHALL join their center pixels, drawn under every other object
- each cube SHALL be accelerated toward the other (12 px/s²), with its speed capped at 44 px/s

Changing either cube's color SHALL end the line and the pull immediately. Each cube keeps its current velocity.

#### Scenario: Pull turns a fleeing cube around
- **WHEN** entangled cubes are moving apart
- **THEN** each slows, reverses, and accelerates back toward the other

### Requirement: Steal in flight

While entangled, a player's direction button SHALL launch a steal instead of steering, provided that player has no steal already in flight and the opponent holds at least one electron. Otherwise the press SHALL do nothing. A steal SHALL take one of the opponent's held electrons uniformly at random, regardless of color. The electron leaves the opponent's orbit at once and moves along the link from the victim's center toward the thief at 40 px/s, drawn in its own color. Each player may have at most one steal in flight, and both players may have one at the same time. A steal SHALL end in exactly one of three ways:
- **capture:** it reaches the thief while still entangled and joins the thief's orbit
- **forfeit:** the shared color breaks first; it bursts off at a random angle from its point on the line and settles as a floater
- **wipe:** a same-color collision scatters it along with everything else

#### Scenario: Capture
- **WHEN** a steal's electron reaches the thief's cube while the cubes still share a color
- **THEN** it joins the thief's orbit, and the thief may launch another steal

#### Scenario: Forfeit by recolor
- **WHEN** either player changes color while a steal is in flight
- **THEN** that electron becomes a free floater, and nobody captures it

#### Scenario: Nothing to steal
- **WHEN** an entangled player presses direction and the opponent holds no electrons
- **THEN** nothing happens, and the cube's velocity is unchanged

### Requirement: Draw order

Each PLAY frame SHALL clear to `COMTEL_OFF` and then draw, in this order:
1. the entanglement link
2. in-flight electrons
3. both cubes, in their colors
4. floating electrons
5. orbiting electrons

Off-screen orbit cells SHALL be skipped, but those electrons still count as held.

#### Scenario: Link never hides objects
- **WHEN** the link crosses an electron or a cube
- **THEN** that object is drawn on top in its own color

