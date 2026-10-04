# plugin-system Specification

## Purpose

Define the standalone plugin architecture for the LED sign firmware: each app is a self-contained plugin composed only against the shared driver core, with exactly one plugin per firmware image and discovery by convention from `src/plugins/`.

## Requirements

### Requirement: Standalone plugin format

Each app SHALL be a standalone plugin: a single C++ source file under `src/plugins/<name>.cpp` that defines its own Arduino `setup()` and `loop()` and contains its own rendering logic. A plugin SHALL be self-contained apart from the shared driver core, and SHALL NOT depend on any other plugin or any shared application/render library. The plugin's identifier SHALL be the file's name stem (`src/plugins/scrolling_text.cpp` has id `scrolling_text`).

#### Scenario: A plugin owns its entry points
- **WHEN** a plugin source file is authored under `src/plugins/`
- **THEN** it defines its own `setup()` and `loop()` functions
- **AND** it contains its own rendering logic rather than calling into a shared render library

#### Scenario: Plugins are independent
- **WHEN** any plugin is built
- **THEN** its only compiled dependency is the shared driver core
- **AND** it does not include or link another plugin or shared app/render code

#### Scenario: Plugin id derives from filename
- **WHEN** a plugin file is named `src/plugins/<name>.cpp`
- **THEN** the plugin is identified everywhere (menu, build selection) by `<name>`

### Requirement: Shared core is the driver plus generic plumbing only

The code shared across plugins and compiled into every plugin image SHALL be the driver core: the `ComtelPanel` hardware driver, and optionally generic, hardware-adjacent utilities with no application behavior (for example input debounce or serial line reading). The shared core SHALL NOT contain application or rendering logic; app/render behavior such as the scrolling-text and bouncing-square logic SHALL live inside the plugin that uses it (as file-local code or a plugin-private header), not as a module other plugins depend on.

#### Scenario: Driver always available
- **WHEN** any plugin is built
- **THEN** the `ComtelPanel` driver is compiled and linked into that image
- **AND** the plugin constructs and drives the panel itself (`begin()`, `service()`, `show()`)

#### Scenario: No shared app or render layer
- **WHEN** a plugin renders to the panel or implements app behavior
- **THEN** that logic resides within the plugin's own source, not in a library shared with other plugins
- **AND** removing or changing one plugin cannot affect another

#### Scenario: Generic plumbing may live in the core
- **WHEN** a utility is purely mechanical and hardware-adjacent (no app/render behavior), such as button debounce or a serial line reader
- **THEN** it MAY be placed in the shared driver core
- **AND** doing so introduces no dependency of one plugin on another, since every plugin already depends on the core

### Requirement: Exactly one plugin per firmware image

A firmware image SHALL contain exactly one plugin. The build SHALL compile the selected plugin's source file together with the shared core and exclude all other plugin files.

#### Scenario: One plugin compiled
- **WHEN** a plugin is selected for a build
- **THEN** only that plugin's `.cpp` from `src/plugins/` is compiled
- **AND** no other `src/plugins/*.cpp` is compiled into the same image

#### Scenario: Two plugins never coexist
- **WHEN** more than one plugin file would be compiled together
- **THEN** the build fails (duplicate `setup()`/`loop()` definitions) rather than producing an ambiguous image

### Requirement: Plugin discovery by convention

The set of available plugins SHALL be defined by the contents of `src/plugins/` — every `*.cpp` file in that directory is an available plugin — with no separate registry file to maintain. A plugin file MAY declare a human-readable label via a one-line descriptor comment; when absent, the plugin id alone identifies it.

#### Scenario: Adding a plugin requires only a new file
- **WHEN** a new `*.cpp` file is added to `src/plugins/`
- **THEN** it becomes an available plugin with no other file needing to be edited

#### Scenario: Optional descriptor label
- **WHEN** a plugin file begins with a descriptor comment (`// @plugin: <text>`)
- **THEN** `<text>` is available as the plugin's human-readable label
- **AND** when no descriptor comment is present, the plugin id serves as its label
