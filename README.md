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

There are two kinds of firmware image (see [Plugins](#plugins)):

- **`launcher`** — every plugin in one image, plus WiFi, a phone-friendly
  control page and over-the-air (OTA) updates. Pick the running plugin and
  change its settings from a browser; no reflash to switch. This is the
  normal way to run the sign.
- **solo** (`<plugin id>`) — one plugin, no networking; it runs at boot. Handy
  for bench work, or a sign with no WiFi.

Both are built from the same plugin files. The flasher lists the targets
(`launcher` first) and builds the one you pick:

```
python tools/flash.py                     # menu → pick → build + upload
python tools/flash.py launcher            # skip the menu (scriptable)
python tools/flash.py bouncer             # a solo image
python tools/flash.py launcher --port COM4    # explicit USB port
python tools/flash.py launcher --no-upload    # compile check only
python tools/flash.py launcher --ota          # upload over WiFi
python tools/flash.py launcher --ota 192.168.1.50   # ... to an explicit host
python tools/flash.py --sync              # regenerate the env block, then exit
```

The flasher finds `pio` even when it isn't on `PATH` (it falls back to the
PlatformIO install under `~/.platformio/penv/`). Plain `pio run -e <env>`
works too, and the PlatformIO IDE's environment switcher lists every target.

### Launcher setup

1. **Create `include/secrets.h`** from the template and fill in the network:

   ```
   cp include/secrets.example.h include/secrets.h
   ```

   It holds `COMTEL_WIFI_SSID`, `COMTEL_WIFI_PASS`, `COMTEL_HOSTNAME` (default
   `comtel`) and an optional `COMTEL_OTA_PASS`. It is gitignored; the launcher
   refuses to build without it.
2. **Flash the launcher over USB once:** `python tools/flash.py launcher`.
3. **Open the page.** On boot the sign prints its IP and
   `http://<hostname>.local/` on serial (115200). Open either from a phone or
   PC on the same network. Tap a plugin to run it; the running plugin's
   settings (if it has any) appear below with an **Apply** button. The last
   plugin chosen is restored after a power cycle. For a stable IP, give the
   sign a DHCP reservation on the router.
4. **Later updates go over WiFi:** `python tools/flash.py launcher --ota`
   (defaults to `<COMTEL_HOSTNAME>.local` and passes `COMTEL_OTA_PASS`). The
   display blanks during the upload, then the sign reboots into the same
   plugin. Only the launcher has OTA — a solo image is flashed over USB.

If the network is down the sign still runs its last plugin and joins once the
access point appears. The page and API are unauthenticated: fine on a trusted
home/lab network, not somewhere public.

The JSON API the page uses: `GET /api/state`, `POST /api/run`
(`{"id":"tetris"}`) and `POST /api/setting`
(`{"id":"scrolling_text","key":"message","value":"HI"}`); see
`src/host/web.cpp`. The page itself is `src/host/web/index.html`; after
editing it run `python tools/embed_web.py` and commit the regenerated
`src/host/web_page.h`.

Non-plugin environment: `diag` — the interactive protocol diagnostics kept for
hardware archaeology (frozen, untouched by the plugin system).

## Using it (the `scrolling_text` plugin)

Run `scrolling_text` (from the launcher page, or flash its solo image), then
open a serial monitor @115200. **Type any line of text → it scrolls on the
sign.** The launcher page offers message, slot, speed, size, bold and italic
as settings. Serial commands:

| Command | Effect |
|---|---|
| `!slot <0..3>` | switch playlist slot (typed text edits the current slot) |
| `!list` | show the playlist |
| `!speed <ms>` | scroll step interval, 5–2000 (lower = faster; default 45) |
| `!size <1\|2>` | text size: 1 = 6×8 centred, 2 = 12×16 full height (default) |
| `!bold [0\|1]` / `!italic [0\|1]` | text style (no argument = toggle) |
| `!stop` / `!go` | pause / resume |
| `!card` | module-alignment test card |
| `!stair` / `!row <0..15>` | row-mapping diagnostics |
| `!shift <0..7>` | lower-half row-decoder offset (calibration; default 0, not saved) |
| `!status`, `!help` | what they say |

Buttons: **BTN1** short = next message, long = pause/resume. **BTN2** short =
cycle speed, long = test card.

The playlist, current slot, size, bold, italic and speed are saved to flash
(NVS) and survive a power cycle.

## Using it (the `fluid` plugin)

Run `fluid` (from the launcher page, or flash its solo image). A landscape
particle fluid runs on its own: blocks of red, green, and blue drop from the
top at a random X, fall, and mix where they overlap (yellow, cyan, magenta,
white). Auto-spawn would fill the sign in about a minute; once the panel is
about half full the bottom row drains so the sim can keep going.

| Button | GPIO | Effect |
|---|---|---|
| BTN1 | 32 | drop a red block |
| BTN2 | 33 | drop a green block |
| extra | 2 | drop a blue block |
| extra 2 | 15 | drain the scene |

## Code layout

| Path | Role |
|---|---|
| `src/driver/comtel_panel.h/.cpp` | `ComtelPanel` — the hardware driver, compiled into **every** image: draw on its Adafruit-GFX canvas, call `show()`; `service()` keeps the display multiplexed |
| `src/driver/button_pins.h` | declaration-only list of the GPIOs plugins may use for buttons (disjoint from the driver's pins) |
| `src/host/plugin.h` | the plugin interface: `Plugin`, `PluginSetting`, `COMTEL_PLUGIN(...)` |
| `src/host/host.cpp`, `registry.*` | the host: Arduino `setup()`/`loop()`, the one panel, plugin registry, switching, command queue |
| `src/host/web.cpp`, `web/index.html`, `web_page.h` | launcher only: WiFi, mDNS, the control page + JSON API, OTA |
| `src/plugins/scrolling_text.cpp` | plugin: the full scrolling-message app (playlist, styles, buttons, NVS, web settings), with its renderer folded in |
| `src/plugins/bouncer.cpp` | plugin: a bouncing 2×2 square — the minimal reference plugin |
| `src/plugins/fluid.cpp` | plugin: landscape particle fluid — primary-colour drops, mixing, auto-drain |
| `src/plugins/tetris.cpp` | plugin: upright 16×96 Tetris, three buttons, score on serial |
| `src/plugins/entanglement.cpp` | plugin: two-player, four-button colour/electron game (plus its private `start_screen_image.h` / `win_screen_image.h`) |
| `src/plugins/button_test.cpp` | diagnostic plugin: one on-glass square per button, edges logged to serial |
| `src/plugins/row_uniformity.cpp` | diagnostic plugin: row-brightness test cards and live A/B of the burst calibration |
| `src/comtel_diag.cpp` | frozen reverse-engineering diagnostic firmware (env `diag`) |
| `include/secrets.example.h` | template for the gitignored `include/secrets.h` (WiFi, hostname, OTA password) |
| `tools/flash.py` | the flasher: discover plugins, sync the env block, lint, build + upload (USB or OTA) |
| `tools/embed_web.py` | gzips the control page into `src/host/web_page.h` |
| `docs/` | protocol spec, wiring, history |
| `archive/` | probe-era tools and docs, kept for posterity |

## Plugins

Each app is a **plugin**: one self-contained `.cpp` under `src/plugins/`. The
**host** (`src/host/`) owns Arduino `setup()`/`loop()`, the single
`ComtelPanel` and `Serial.begin()`; a plugin implements `start` and `tick`
against the panel the host passes in, and registers itself. Exactly one plugin
is active at a time. A plugin depends on nothing but the driver and the host
interface — never on another plugin or a shared render library.

```cpp
// @plugin: Bouncing 2x2 square
#include <Arduino.h>
#include "../host/plugin.h"

namespace {                 // EVERYTHING file-local goes in here, types too

int x, y;
uint32_t last;

void start(ComtelPanel &panel) {   // fresh start: reset ALL state
  x = 0; y = 0; last = millis();
}

void tick(ComtelPanel &panel) {    // the old loop(), minus panel.service()
  if (millis() - last < 40) return;
  last = millis();
  panel.canvas.fillScreen(COMTEL_OFF);
  panel.canvas.fillRect(x, y, 2, 2, COMTEL_WHITE);
  panel.show();
}

}  // namespace

COMTEL_PLUGIN(bouncer, "Bouncing 2x2 square", start, tick, nullptr);
```

Authoring rules:

- **The filename is the plugin id**, and `COMTEL_PLUGIN`'s first argument
  must match it: `src/plugins/bouncer.cpp` → `COMTEL_PLUGIN(bouncer, ...)`.
  The label must match the `// @plugin:` comment. The flasher warns about
  either mismatch.
- **Wrap the whole plugin in an anonymous namespace.** The launcher links
  every plugin into one image, so two plugins' `struct Button`s (or `draw()`s)
  would otherwise collide.
- **Never define `setup()`/`loop()`, construct a `ComtelPanel`, call
  `panel.begin()`/`service()` or `Serial.begin()`** — the host does.
- **`start()` is a fresh start, every time.** It runs at boot and whenever the
  plugin is selected (including re-selecting it), and nothing resumes. Static
  initialisers only ran once, so re-set *every* variable here, set `pinMode`s,
  and re-arm input. The host has already reset the driver calibration and the
  canvas text/cursor state, and blanked the panel.
- **`tick()` must return promptly** — the display is only multiplexed between
  ticks. An optional `stop()` (or `nullptr`) runs when switching away.
- **Settings are optional.** Pass a `static const PluginSetting` array as the
  last `COMTEL_PLUGIN` argument and the page renders a form for it: `Text`
  (max length), `Int` (min..max), `Bool` or `Choice` (`"1|2"`). Each has a
  `get(char *out, size_t n)` and a `bool set(const char *value)`; the host
  validates first, `set` may still reject, and persisting is up to the plugin.
  See `scrolling_text.cpp`.
- **One `.cpp` per plugin.** Helpers live in that file or a plugin-private
  `.h` it `#include`s — never a sibling `.cpp` (discovery would treat it as
  another plugin).
- **Buttons:** take pin numbers from `"../driver/button_pins.h"`; the plugin
  still does its own `pinMode`, reads and debounce. Serial input belongs to
  the active plugin; the host only prints `host: ...` status lines.

Dropping a `.cpp` in `src/plugins/` is all it takes to add a plugin: it is
compiled into the launcher, and the flasher regenerates its solo
`[env:<id>]` into a marked, machine-owned block in `platformio.ini` (run
`python tools/flash.py --sync`, or let any flasher run auto-sync it). Then
`python tools/flash.py launcher --ota` puts it on the sign.

**Colour:** `panel.canvas` is a 96×16 `GFXcanvas8` whose low three bits are an
RGB on/off mask. Draw with the named palette in `comtel_panel.h` —
`COMTEL_OFF`, `RED`, `GREEN`, `YELLOW`, `BLUE`, `MAGENTA`, `CYAN`, `WHITE` —
not bare numbers (a bare `1` is red, not white). Each channel is either off or
full brightness; the hardware's 6-bit per-channel brightness (see PROTOCOL.md)
is not exposed yet.
