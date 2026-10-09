## ADDED Requirements

### Requirement: Typed settings declaration

A plugin MAY declare settings as a static array of `PluginSetting` entries in its descriptor. Each entry SHALL have:
- `key`, unique within the plugin
- `label`
- `type`: one of `Text`, `Int`, `Bool` or `Choice`
- type constraints: `min`/`max` for `Int`; maximum length in `max` for `Text`; pipe-separated `options` for `Choice`
- a `get` callback that writes the current value as text
- a `set` callback that applies a text value and returns whether it was accepted

A plugin with no settings SHALL declare none.

#### Scenario: Plugin without settings
- **WHEN** `bouncer` is listed
- **THEN** its settings list is empty

### Requirement: Values are validated before applying

Setting values SHALL travel as text. Before queuing a value, the host SHALL reject it if:
- `Int`: it is not an integer within `[min, max]`
- `Bool`: it is not `0`, `1`, `true` or `false`
- `Choice`: it is not one of the options
- `Text`: it is longer than `max`

The plugin's `set` SHALL validate again, and MAY reject a value by returning false. A rejected value SHALL leave the plugin's state unchanged.

#### Scenario: Out-of-range integer
- **WHEN** `speed` (Int 5..2000) is set to `3`
- **THEN** the request is rejected, and the speed is unchanged

#### Scenario: Valid choice
- **WHEN** `size` (Choice `1|2`) is set to `1`
- **THEN** the plugin applies size 1

### Requirement: Settings apply only to the active plugin

Settings SHALL be readable and writable only for the active plugin. The value is applied on the main loop through the plugin's `set`, and the state snapshot is republished afterwards. Any persistence of a setting SHALL be the plugin's own responsibility.

#### Scenario: Setting an inactive plugin
- **WHEN** a setting is sent for a plugin that is not active
- **THEN** it is rejected, and nothing changes

#### Scenario: Snapshot reflects the change
- **WHEN** a setting is applied
- **THEN** the next state read shows the new value from the plugin's `get`
