## ADDED Requirements

### Requirement: Runs under the plugin host

The `entanglement` plugin SHALL register with the plugin host and declare no settings. Every `start()` SHALL behave like the former cold boot: enter START and push the start image, discard any game in progress (cubes, electrons, steals and timers), and disarm input until all four buttons have been seen released. `tick()` SHALL do the work the former `loop()` did after `panel.service()`.

#### Scenario: Switching back shows the start screen
- **WHEN** a game is in PLAY, the user switches to another plugin, then back to `entanglement`
- **THEN** the start image is shown, and no game runs until a button is pressed

#### Scenario: Held button across the switch
- **WHEN** a player is holding a button at the moment `entanglement` is started
- **THEN** that button does not start a game until every button has been released once
