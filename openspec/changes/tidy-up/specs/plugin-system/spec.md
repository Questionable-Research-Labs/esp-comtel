## MODIFIED Requirements

### Requirement: Standalone plugin format

Each app SHALL be a standalone plugin: a single C++ source file at `src/plugins/<name>.cpp` that defines its own Arduino `setup()` and `loop()` and contains its own rendering logic. Apart from the shared driver core, a plugin SHALL be self-contained, and SHALL NOT depend on any other plugin or on any shared application or render library. A plugin MAY `#include` plugin-private headers stored alongside it in `src/plugins/`, such as data-only image arrays. These headers SHALL be `.h` files, never `.cpp`, so plugin discovery does not mistake them for plugins, and no other plugin SHALL include them. The plugin's identifier SHALL be the file's name stem (`src/plugins/scrolling_text.cpp` has id `scrolling_text`).

#### Scenario: A plugin owns its entry points
- **WHEN** a plugin source file is authored in `src/plugins/`
- **THEN** it defines its own `setup()` and `loop()` functions
- **AND** it contains its own rendering logic rather than calling into a shared render library

#### Scenario: Plugins are independent
- **WHEN** any plugin is built
- **THEN** its only compiled dependency is the shared driver core
- **AND** it does not include or link another plugin or shared app/render code

#### Scenario: Plugin-private data header
- **WHEN** a plugin needs bulky data, such as a screen image (`entanglement` uses `start_screen_image.h` and `win_screen_image.h`)
- **THEN** it keeps that data in a `.h` file in `src/plugins/` that only it includes
- **AND** the flasher does not list that header as a plugin

#### Scenario: Plugin id derives from filename
- **WHEN** a plugin file is named `src/plugins/<name>.cpp`
- **THEN** the plugin is identified everywhere (menu, build selection) by `<name>`

### Requirement: Shared core is the driver plus generic plumbing only

The code shared across plugins and compiled into every plugin image SHALL be the driver core in `src/driver/`: the `ComtelPanel` hardware driver (`comtel_panel.h/.cpp`) and generic, hardware-adjacent declarations or utilities with no application behavior. Today the only such declaration is the button pin header `button_pins.h`. The shared core SHALL NOT contain application or rendering logic. App and render behavior, such as scrolling text, the bouncing square or game logic, SHALL live inside the plugin that uses it, as file-local code or a plugin-private header, not in a module other plugins depend on.

#### Scenario: Driver always available
- **WHEN** any plugin is built
- **THEN** the `ComtelPanel` driver is compiled and linked into that image
- **AND** the plugin constructs and drives the panel itself (`begin()`, `service()`, `show()`)

#### Scenario: No shared app or render layer
- **WHEN** a plugin renders to the panel or implements app behavior
- **THEN** that logic lives in the plugin's own source, not in a library shared with other plugins
- **AND** removing or changing one plugin cannot affect another

#### Scenario: Shared button pin names
- **WHEN** a plugin reads buttons
- **THEN** it takes pin numbers from `src/driver/button_pins.h` instead of hard-coding GPIO numbers
- **AND** that header only declares pin names; the plugin still owns `pinMode`, reading and debouncing

#### Scenario: Generic plumbing may live in the core
- **WHEN** a utility is purely mechanical and hardware-adjacent, with no app or render behavior (for example button debounce or a serial line reader)
- **THEN** it MAY be placed in the shared driver core
- **AND** doing so introduces no dependency of one plugin on another, since every plugin already depends on the core
