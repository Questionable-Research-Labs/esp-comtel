## MODIFIED Requirements

### Requirement: Tetris plugin entry point

The system SHALL provide a `tetris` plugin at `src/plugins/tetris.cpp` with the descriptor `// @plugin: Tetris`, registered with the plugin host. Its `tick()` SHALL call `panel.show()` only when the frame has changed (or on a game-over flash toggle); the host keeps the display serviced between ticks. Every `start()` SHALL begin a new game: empty board, score 0, fresh 7-bag, game-over state cleared, and input disarmed until all three buttons have been seen released.

#### Scenario: No redundant frames
- **WHEN** the game is idle between gravity steps
- **THEN** `tick()` issues no `show()`

#### Scenario: Switching back starts a new game
- **WHEN** the user leaves Tetris mid-game and later selects it again
- **THEN** a new game starts with an empty board and score 0
