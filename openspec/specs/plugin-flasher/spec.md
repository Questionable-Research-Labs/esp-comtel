# plugin-flasher Specification

## Purpose

Define the flasher tool that discovers plugins in `src/plugins/`, generates one PlatformIO build environment per plugin, and builds and uploads a single user-selected plugin to the ESP32.

## Requirements

### Requirement: List available plugins

The flasher tool SHALL discover the available plugins by scanning `src/plugins/*.cpp` and present them to the user, showing each plugin's id and, when present, its descriptor label.

#### Scenario: Interactive menu lists plugins
- **WHEN** the user runs the flasher with no plugin argument
- **THEN** it prints a numbered menu of every plugin found in `src/plugins/`
- **AND** each entry shows the plugin id and its descriptor label when one exists

#### Scenario: No plugins found
- **WHEN** `src/plugins/` contains no `*.cpp` files
- **THEN** the flasher reports that no plugins were found and exits without building

### Requirement: Select a plugin to flash

The flasher SHALL let the user choose exactly one plugin to flash, either by picking from the interactive menu or by passing the plugin id as an argument for non-interactive use.

#### Scenario: Selection from the menu
- **WHEN** the user picks a menu entry
- **THEN** the flasher proceeds to build and upload that single plugin

#### Scenario: Selection by argument
- **WHEN** the user runs the flasher with a plugin id argument
- **THEN** the flasher skips the menu and targets that plugin

#### Scenario: Unknown plugin id
- **WHEN** the user passes a plugin id that does not exist in `src/plugins/`
- **THEN** the flasher reports the error and exits non-zero without building

### Requirement: Generate one build environment per plugin

The flasher SHALL maintain, in a clearly marked machine-owned region of `platformio.ini`, one build environment per plugin. Each environment SHALL compile the shared driver core plus only that plugin's source file. The region SHALL be derived from the contents of `src/plugins/` and SHALL be refreshed when that set changes; regeneration SHALL leave hand-written environments (e.g. `diag`) and any content outside the marked region unchanged.

#### Scenario: One environment per plugin
- **WHEN** the environment block is generated from `src/plugins/`
- **THEN** there is exactly one build environment per plugin, named for the plugin
- **AND** each environment's source filter includes the driver and only that plugin's file, excluding every other plugin file

#### Scenario: Refresh on plugin set change
- **WHEN** a plugin is added to or removed from `src/plugins/` and the flasher runs
- **THEN** the marked region is refreshed so environments match the current plugin set
- **AND** content outside the marked region is preserved unchanged

#### Scenario: Selecting does not rewrite the file
- **WHEN** a plugin is selected for flashing and the plugin set is unchanged
- **THEN** flashing proceeds without rewriting `platformio.ini`

### Requirement: Build and upload the selected plugin

The flasher SHALL build the selected plugin's environment and upload it to the ESP32, reporting success or failure. It SHALL locate the `pio` executable even when it is not on `PATH`, SHALL allow the upload port to be specified, and MAY offer a build-only mode that skips upload.

#### Scenario: Successful flash
- **WHEN** the selected plugin builds successfully
- **THEN** the flasher uploads the image to the ESP32 via that plugin's environment
- **AND** reports success

#### Scenario: Locating the build tool
- **WHEN** `pio` is not on `PATH`
- **THEN** the flasher resolves it from the known PlatformIO install location
- **AND** if it still cannot be found, reports a clear error and exits non-zero

#### Scenario: Build or upload failure
- **WHEN** the build or upload fails
- **THEN** the flasher surfaces the underlying error and exits non-zero

#### Scenario: Explicit upload port
- **WHEN** the user specifies an upload port
- **THEN** the flasher uses that port for upload
- **AND** when none is specified it relies on automatic port detection
