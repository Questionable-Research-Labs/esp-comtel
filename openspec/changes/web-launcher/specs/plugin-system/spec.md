## MODIFIED Requirements

### Requirement: Standalone plugin format

Each app SHALL be a standalone plugin: a single C++ source file at `src/plugins/<name>.cpp` that implements the plugin-host interface (`start`, `tick`, optional `stop`, optional settings) and registers itself with `COMTEL_PLUGIN(...)`. A plugin SHALL NOT define Arduino `setup()` or `loop()`, and SHALL NOT construct its own `ComtelPanel`; it draws on the panel the host passes in. All of the plugin's own code (state, types, helpers) SHALL be inside an anonymous namespace, so that plugins compiled into the same image cannot collide. Apart from the shared core, a plugin SHALL be self-contained, and SHALL NOT depend on any other plugin or on any shared application or render library. A plugin MAY `#include` plugin-private headers stored alongside it in `src/plugins/`, such as data-only image arrays. These headers SHALL be `.h` files, never `.cpp`, so plugin discovery does not mistake them for plugins, and no other plugin SHALL include them. The plugin's identifier SHALL be the file's name stem (`src/plugins/scrolling_text.cpp` has id `scrolling_text`), and the id passed to `COMTEL_PLUGIN` SHALL equal it.

#### Scenario: A plugin implements the host interface
- **WHEN** a plugin source file is authored in `src/plugins/`
- **THEN** it provides `start` and `tick` and registers them with `COMTEL_PLUGIN`
- **AND** it contains its own rendering logic rather than calling into a shared render library

#### Scenario: Plugins are independent
- **WHEN** any plugin is built
- **THEN** its only compiled dependency is the shared core (driver and host)
- **AND** it does not include or link another plugin or shared app/render code

#### Scenario: Same-named types do not collide
- **WHEN** two plugins each define a `struct Button` with different members and are linked into the launcher image
- **THEN** the build succeeds, and each plugin uses its own definition

#### Scenario: Plugin-private data header
- **WHEN** a plugin needs bulky data, such as a screen image (`entanglement` uses `start_screen_image.h` and `win_screen_image.h`)
- **THEN** it keeps that data in a `.h` file in `src/plugins/` that only it includes
- **AND** the flasher does not list that header as a plugin

#### Scenario: Plugin id derives from filename
- **WHEN** a plugin file is named `src/plugins/<name>.cpp`
- **THEN** the plugin is identified everywhere (menu, build selection, web UI, API) by `<name>`

### Requirement: Shared core is the driver plus generic plumbing only

The code shared across plugins and compiled into every plugin image SHALL be:
- the driver core in `src/driver/`: the `ComtelPanel` hardware driver (`comtel_panel.h/.cpp`) and generic, hardware-adjacent declarations with no application behaviour, such as the button pin header `button_pins.h`
- the plugin host in `src/host/`, which owns the entry points, plugin registry and lifecycle and, in the launcher build, networking

The shared core SHALL NOT contain application or rendering logic. App and render behaviour, such as scrolling text, the bouncing square or game logic, SHALL live inside the plugin that uses it.

#### Scenario: Driver always available
- **WHEN** any plugin is built
- **THEN** the `ComtelPanel` driver and the host are compiled into that image
- **AND** the host constructs the panel and calls `begin()`/`service()`, and the plugin draws and calls `show()`

#### Scenario: No shared app or render layer
- **WHEN** a plugin renders to the panel or implements app behaviour
- **THEN** that logic lives in the plugin's own source, not in a library shared with other plugins
- **AND** removing or changing one plugin cannot affect another

#### Scenario: Shared button pin names
- **WHEN** a plugin reads buttons
- **THEN** it takes pin numbers from `src/driver/button_pins.h` instead of hard-coding GPIO numbers
- **AND** that header only declares pin names; the plugin still owns `pinMode`, reading and debouncing

#### Scenario: Generic plumbing may live in the core
- **WHEN** a utility is purely mechanical and hardware-adjacent, with no app or render behaviour (for example button debounce or a serial line reader)
- **THEN** it MAY be placed in the shared driver core
- **AND** doing so introduces no dependency of one plugin on another, since every plugin already depends on the core

### Requirement: Exactly one plugin per firmware image

There SHALL be two kinds of firmware image:
- **launcher image:** contains the host with networking and every plugin in `src/plugins/`
- **solo image:** contains the host without networking and exactly one plugin

A solo build SHALL compile only the selected plugin's source together with the shared core, and exclude every other plugin file. In both kinds of image, exactly one plugin SHALL be active at a time.

#### Scenario: Solo build compiles one plugin
- **WHEN** a plugin's solo env is built
- **THEN** only that plugin's `.cpp` from `src/plugins/` is compiled

#### Scenario: Launcher contains all plugins
- **WHEN** the launcher env is built
- **THEN** every `src/plugins/*.cpp` is compiled into the image and registered with the host

### Requirement: Plugin discovery by convention

The set of available plugins SHALL be defined by the contents of `src/plugins/`: every `*.cpp` file in that directory is an available plugin, and there is no separate registry file to maintain. At build time the flasher discovers plugins from the directory listing. At runtime the host discovers them through each file's `COMTEL_PLUGIN` registration. A plugin file MAY declare a human-readable label in a one-line descriptor comment, and its `COMTEL_PLUGIN` label SHALL match that comment. When the comment is absent, the plugin id serves as its label.

#### Scenario: Adding a plugin requires only a new file
- **WHEN** a new `*.cpp` file is added to `src/plugins/`
- **THEN** it becomes available in the flasher, the launcher image and the web UI, with no other file needing to be edited

#### Scenario: Optional descriptor label
- **WHEN** a plugin file begins with a descriptor comment (`// @plugin: <text>`)
- **THEN** `<text>` is available as the plugin's human-readable label
- **AND** when no descriptor comment is present, the plugin id serves as its label
