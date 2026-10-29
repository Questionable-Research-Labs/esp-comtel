# button-pins Specification

## Purpose
Define the shared, declaration-only header of ESP32 GPIOs that plugins may use for button input, disjoint from the display driver's pins.
## Requirements
### Requirement: Shared declaration-only button pin header

The driver core SHALL provide `src/driver/button_pins.h` as the single source of truth for which GPIOs plugins may use for button input. The header SHALL only declare named constants: it SHALL NOT configure pins, read pins, or enable pull-ups. Each plugin SHALL own its own `pinMode`, reading and debouncing.

#### Scenario: Including the header has no side effects
- **WHEN** a plugin `#include`s `button_pins.h` and uses none of its constants
- **THEN** no GPIO is configured and no code is generated

#### Scenario: Plugin claims a pin
- **WHEN** a plugin uses `BTN_PIN_1`
- **THEN** that plugin calls `pinMode(BTN_PIN_1, INPUT_PULLUP)` itself

### Requirement: Reserved pin set

`button_pins.h` SHALL declare the following pins. All of them SHALL be disjoint from the `ComtelPanel` driver's GPIOs.

| Constant | GPIO | Pull-up | Caveat |
|---|---:|---|---|
| `BTN_PIN_1` | 32 | internal | primary button |
| `BTN_PIN_2` | 33 | internal | primary button |
| `BTN_PIN_EXTRA` | 2 | internal | strapping pin; must not be held low at reset |
| `BTN_PIN_EXTRA_2` | 15 | internal | strapping pin; must not be held low at reset |
| `BTN_PIN_INPUT_ONLY_1` | 34 | **external** | input-only, no internal pull-up |
| `BTN_PIN_INPUT_ONLY_2` | 35 | **external** | input-only, no internal pull-up |
| `BTN_PIN_INPUT_ONLY_3` | 36 | **external** | input-only (VP) |
| `BTN_PIN_INPUT_ONLY_4` | 39 | **external** | input-only (VN) |

Buttons SHALL be wired between the pin and GND, so a press reads LOW.

#### Scenario: Internal pull-up pin
- **WHEN** a button on GPIO 32 is wired to GND and configured `INPUT_PULLUP`
- **THEN** it reads HIGH when released and LOW when pressed, with no external resistor

#### Scenario: Input-only pin
- **WHEN** a plugin uses GPIO 34–39
- **THEN** the button needs an external pull-up (10 kΩ to 3.3 V), because `INPUT_PULLUP` has no effect on those pins

#### Scenario: Strapping pin at boot
- **WHEN** a momentary button on GPIO 2 or 15 is not pressed during reset
- **THEN** the ESP32 boots normally and the pin works as an input afterward

#### Scenario: Preference order
- **WHEN** a plugin needs N buttons
- **THEN** it uses the primary pair (32, 33) first, then the strapping pins (2, 15), then the input-only pins

