## MODIFIED Requirements

### Requirement: List available plugins

The flasher tool SHALL discover the available plugins by scanning `src/plugins/*.cpp`, and present them to the user with each plugin's id and, when present, its descriptor label. The menu SHALL offer the `launcher` target (all plugins, web UI) as the first entry, followed by each plugin's solo target.

#### Scenario: Interactive menu lists plugins
- **WHEN** the user runs the flasher with no target argument
- **THEN** it prints a numbered menu: first `launcher`, then every plugin found in `src/plugins/`
- **AND** each plugin entry shows its id and, when one exists, its descriptor label

#### Scenario: No plugins found
- **WHEN** `src/plugins/` contains no `*.cpp` files
- **THEN** the flasher reports that no plugins were found and exits without building

### Requirement: Select a plugin to flash

The flasher SHALL let the user choose exactly one target, `launcher` or a plugin id, either by picking from the interactive menu or by passing the target as an argument for non-interactive use.

#### Scenario: Selection from the menu
- **WHEN** the user picks a menu entry
- **THEN** the flasher builds and uploads that target

#### Scenario: Selection by argument
- **WHEN** the user runs `python tools/flash.py launcher` or `python tools/flash.py <id>`
- **THEN** the flasher skips the menu and targets it

#### Scenario: Unknown plugin id
- **WHEN** the user passes a target that is neither `launcher` nor a plugin in `src/plugins/`
- **THEN** the flasher reports the error and exits non-zero without building

### Requirement: Generate one build environment per plugin

The flasher SHALL maintain, in a clearly marked machine-owned region of `platformio.ini`:
- a shared base section, with the driver and host sources, the Adafruit GFX dependency and the `min_spiffs.csv` partition table
- one `[env:launcher]`, which compiles the base plus every plugin, with networking enabled and the web libraries as dependencies
- one solo environment per plugin, named for the plugin, which compiles the base plus only that plugin's source, with networking disabled

The region SHALL be derived from the contents of `src/plugins/`, and SHALL be refreshed when that set changes. Regeneration SHALL leave hand-written environments (e.g. `diag`) and any content outside the marked region unchanged.

#### Scenario: Launcher plus one solo environment per plugin
- **WHEN** the environment block is generated from `src/plugins/`
- **THEN** it contains `[env:launcher]`, plus exactly one solo environment per plugin, named for the plugin
- **AND** each solo environment's source filter includes the driver, the host and only that plugin's file

#### Scenario: Refresh on plugin set change
- **WHEN** a plugin is added to or removed from `src/plugins/` and the flasher runs
- **THEN** the marked region is refreshed so the solo environments match the current plugin set
- **AND** content outside the marked region is preserved unchanged

#### Scenario: Selecting does not rewrite the file
- **WHEN** a target is selected for flashing and the plugin set is unchanged
- **THEN** flashing proceeds without rewriting `platformio.ini`

## ADDED Requirements

### Requirement: Over-the-air upload

The flasher SHALL accept `--ota [host]` to upload the selected target over WiFi with the espota protocol instead of USB. The default host SHALL be `<COMTEL_HOSTNAME>.local`, read from `include/secrets.h`. The OTA password SHALL be passed through when `COMTEL_OTA_PASS` is defined. If the device can't be reached, the flasher SHALL report the error and exit non-zero.

#### Scenario: OTA by default hostname
- **WHEN** the user runs `python tools/flash.py launcher --ota` with hostname `comtel` in `secrets.h`
- **THEN** the launcher is built and uploaded to `comtel.local` over WiFi

#### Scenario: Unreachable device
- **WHEN** the OTA target does not answer
- **THEN** the flasher surfaces the upload error and exits non-zero
