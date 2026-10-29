## ADDED Requirements

### Requirement: Host owns the Arduino entry points

The firmware SHALL include a plugin host in `src/host/` that defines the only Arduino `setup()` and `loop()`. The host SHALL own the single `ComtelPanel` instance and SHALL call `Serial.begin(115200)` and `panel.begin()` exactly once at boot. On every pass, `loop()` SHALL call `panel.service()` first, then apply any queued commands, then call the active plugin's `tick()`.

#### Scenario: Display serviced every pass
- **WHEN** a plugin is active
- **THEN** each `loop()` pass calls `panel.service()` before the plugin's `tick()`

#### Scenario: Plugins do not define entry points
- **WHEN** any plugin is compiled
- **THEN** it defines no `setup()` or `loop()`, and the image links with the host's

### Requirement: Plugin interface and self-registration

A plugin SHALL describe itself with a `Plugin` descriptor declared in `src/host/plugin.h`:
- `id`, equal to its filename stem
- `label`
- `start(ComtelPanel&)`
- `tick(ComtelPanel&)`
- an optional `stop()`
- an optional settings array

A plugin SHALL register its descriptor with the `COMTEL_PLUGIN(...)` macro. Registration SHALL NOT depend on static-initialisation order across files. The host SHALL list registered plugins sorted by id. Two plugins with the same id SHALL be a startup error reported on Serial; the duplicate SHALL be ignored.

#### Scenario: Drop-in registration
- **WHEN** a new plugin file containing `COMTEL_PLUGIN(...)` is added to `src/plugins/` and the launcher is rebuilt
- **THEN** the host lists it with no other file edited

#### Scenario: Sorted listing
- **WHEN** the host lists plugins
- **THEN** they appear in ascending id order

### Requirement: Exactly one active plugin

At most one plugin SHALL be active at any time. Only the active plugin's `tick()` SHALL be called. `tick()` SHALL return promptly (it is non-blocking, like the former `loop()` body), so the display keeps being serviced.

#### Scenario: Inactive plugins are idle
- **WHEN** `tetris` is active
- **THEN** no other plugin's `tick()` runs

### Requirement: Switching is a fresh start

When the host switches to a plugin, it SHALL perform these steps in order:
1. call the current plugin's `stop()`, if it has one
2. reset every `ComtelPanel` calibration field to its driver default
3. clear the canvas to `COMTEL_OFF` and `show()` it
4. call the new plugin's `start()`

`start()` SHALL fully re-initialise the plugin: all of its state, its pin modes, and input arming. Any behaviour that a plugin's spec describes as happening "at boot" SHALL happen at every `start()`. Switching to the plugin that is already active SHALL also restart it.

#### Scenario: Return to a game starts a new one
- **WHEN** `tetris` is active mid-game, the user switches to `bouncer`, then back to `tetris`
- **THEN** Tetris starts with an empty board and score 0

#### Scenario: Calibration does not leak
- **WHEN** `row_uniformity` sets `scanDuringBurst = false` and the user switches to `scrolling_text`
- **THEN** `scrolling_text` runs with `scanDuringBurst = true` and every other default

#### Scenario: Re-selecting restarts
- **WHEN** the user selects the plugin that is already running
- **THEN** it is stopped and started fresh

### Requirement: Active plugin survives reboot

In the launcher build, the host SHALL save the active plugin id to NVS (namespace `host`, key `active`), but only when the id changes. At boot it SHALL start the saved plugin. If nothing is saved, or the saved id is not registered, it SHALL start the first plugin in id order.

#### Scenario: Power cycle
- **WHEN** `entanglement` is active and the sign is power-cycled
- **THEN** `entanglement` starts after boot

#### Scenario: Saved plugin removed
- **WHEN** the saved id no longer exists in the firmware
- **THEN** the first plugin in id order starts, and Serial reports the fallback

### Requirement: Commands are applied on the main loop

Requests from outside the main loop (the web server) SHALL reach the host as commands on a bounded queue. The host SHALL drain and apply them on the main loop, between `tick()` calls. Code outside the main loop SHALL NOT call plugin functions or `ComtelPanel` methods. The host SHALL republish a state snapshot (active id, plugin list, current setting values) after every applied command, for readers outside the loop.

#### Scenario: Switch from the web
- **WHEN** a switch command is queued while a plugin's `tick()` is running
- **THEN** the switch is applied after that `tick()` returns, on the main loop

#### Scenario: Queue full
- **WHEN** the queue is full
- **THEN** the new command is rejected and the requester is told to retry

### Requirement: Host leaves Serial input to plugins

The host SHALL NOT read Serial input; the active plugin may. The host SHALL print one-line status messages for boot, network address, switches and errors.

#### Scenario: Plugin console still works
- **WHEN** `scrolling_text` is active and the user types a line on Serial
- **THEN** `scrolling_text` receives and handles it

### Requirement: Solo builds

In a solo build (`COMTEL_WEB=0`), the host SHALL include no WiFi, web server, mDNS or OTA code. It SHALL NOT read or write the `host` NVS namespace. It SHALL start the single registered plugin at boot.

#### Scenario: Solo tetris
- **WHEN** the `tetris` solo env is flashed
- **THEN** the sign boots straight into Tetris, and no network code is linked
