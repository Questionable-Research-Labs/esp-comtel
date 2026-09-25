# ledsign — ESP32 driver for a Comtel 96×16 LED sign

Drives an old Comtel (NZ) LED sign — six daisy-chained 16×16 RGB modules —
directly from an ESP32, replacing the original CPU board. The panel's
undocumented protocol was reverse-engineered from scratch; the full spec is in
[docs/PROTOCOL.md](docs/PROTOCOL.md), and the story of how it was cracked (a
good read) is in [docs/HISTORY.md](docs/HISTORY.md).

## Hardware

- ESP32 devkit (esp32dev / WROOM-32) → 2× 74HCT245 level shifters → the
  panel's 20-pin ribbon. Wiring: [docs/wiring.md](docs/wiring.md), ribbon
  reference: [docs/ribbon-pinout.md](docs/ribbon-pinout.md).
- Two optional push buttons: GPIO 32 and GPIO 33, each to GND (internal
  pull-ups). Pins reserved for button input (and the extras for plugins that
  need more) are defined in
  [src/driver/button_pins.h](src/driver/button_pins.h).
- Panels keep their own 5 V supplies; the ribbon carries signals + ground.

## Build & flash

PlatformIO. The driver ESP32 enumerates as a CP210x (COM4 on the dev PC).

The sign runs **one plugin per firmware image**, chosen at build time (see
[Plugins](#plugins)). Each plugin is its own PlatformIO environment, so flash
one by naming its env:

```
pio run -e scrolling_text -t upload --upload-port COM4
```

Or use the flasher, which lists the plugins and builds the one you pick:

```
python tools/flash.py                 # menu → pick → build + upload
python tools/flash.py bouncer         # skip the menu (scriptable)
python tools/flash.py bouncer --port COM4     # explicit upload port
python tools/flash.py bouncer --no-upload     # compile check only
python tools/flash.py --sync          # regenerate the env block, then exit
```

The flasher finds `pio` even when it isn't on `PATH` (it falls back to the
PlatformIO install under `~/.platformio/penv/`).

**Switching apps means re-flashing** — there is no runtime mode toggle; pick a
different plugin and flash it. The PlatformIO IDE's environment switcher also
lists every plugin, so it doubles as a GUI picker.

Non-plugin environment: `diag` — the interactive protocol diagnostics kept for
hardware archaeology (frozen, untouched by the plugin system).

## Using it (the `scrolling_text` plugin)

Flash `scrolling_text`, then open a serial monitor @115200. **Type any line of
text → it scrolls on the sign.** Commands:

| Command | Effect |
|---|---|
| `!slot <0..3>` | switch playlist slot (typed text edits the current slot) |
| `!list` | show the playlist |
| `!speed <ms>` | scroll step interval (lower = faster) |
| `!stop` / `!go` | pause / resume |
| `!card` | module-alignment test card |
| `!shift <0..7>` | lower-half row-decoder offset (calibration; default 1) |
| `!status`, `!help` | what they say |

Buttons: **BTN1** short = next message, long = pause/resume. **BTN2** short =
cycle speed, long = test card.

## Code layout

| Path | Role |
|---|---|
| `src/driver/comtel_panel.h/.cpp` | `ComtelPanel` — the hardware driver, compiled into **every** plugin: draw on its Adafruit-GFX canvas, call `show()`; `service()` keeps the display multiplexed |
| `src/plugins/scrolling_text.cpp` | plugin: the full scrolling-message app (playlist, styles, buttons, NVS), with its renderer folded in |
| `src/plugins/bouncer.cpp` | plugin: a bouncing 2×2 square — the minimal reference plugin |
| `src/comtel_diag.cpp` | frozen reverse-engineering diagnostic firmware (env `diag`) |
| `tools/flash.py` | the flasher: discover plugins, sync the env block, build + upload |
| `docs/` | protocol spec, wiring, history |
| `archive/` | probe-era tools and docs, kept for posterity |

## Plugins

Each app is a **plugin**: one self-contained `.cpp` under `src/plugins/`. A
plugin owns its own `setup()`/`loop()` and its own rendering logic, and depends
on nothing but the `ComtelPanel` driver — never on another plugin or a shared
render library. Exactly one plugin is compiled into a firmware image (two would
collide on the `setup`/`loop` symbols).

Authoring convention:

- **The filename is the plugin id.** `src/plugins/bouncer.cpp` → id `bouncer`.
- **One `.cpp` per plugin.** Render helpers live in that file (file-local
  classes/functions) or a plugin-private header it `#include`s — never as a
  sibling `.cpp` (discovery would treat it as another plugin).
- **Depend only on the driver:** `#include "../driver/comtel_panel.h"`. Copying
  an existing plugin as a starting point is fine — independence is the point.
- **Optional label:** a first-line `// @plugin: <text>` comment gives the
  flasher a human-readable description next to the id.

Dropping a `.cpp` in `src/plugins/` is all it takes to add a plugin: discovery
is by directory listing, and the flasher regenerates one `[env:<id>]` per
plugin into a marked, machine-owned block in `platformio.ini` (run
`python tools/flash.py --sync` after adding or removing one, or just let any
flasher run auto-sync it).

The panel supports 6-bit brightness per colour channel (262k colours); the
current plugins render monochrome white. Colour canvas support is the obvious
next upgrade — see PROTOCOL.md for the encoding.
