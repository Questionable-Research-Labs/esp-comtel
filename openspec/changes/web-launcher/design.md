## Context

Today each plugin (`src/plugins/<id>.cpp`) is a complete Arduino sketch with its own `setup()`/`loop()` and its own `static ComtelPanel panel`, and exactly one is compiled per image. The `plugin-launcher` change (archived 2026-07-22) deliberately removed runtime switching. This change brings it back, driven from a web page, while keeping the single-file plugin format so other people can contribute plugins.

Constraints from the code:
- **The display is CPU-scanned.** `service()` walks 8 scan addresses with `delayMicroseconds(rowHoldUs)`, and `show()` masks interrupts during bursts. Anything that starves the loop or preempts the scan causes flicker or uneven row brightness. `service()` only touches the ADDR lines and never reads the canvas: the modules latch pixel data themselves.
- **Plugins reach into the driver.** `row_uniformity` changes `burstClocks` / `scanDuringBurst`, and `scrolling_text` changes `rowShiftLo` (`!shift`).
- **Plugins arm their input at boot.** `tetris` and `entanglement` ignore buttons until all have been seen released once.
- **Plugins declare same-named types with different bodies** (`Btn`, `Button`, `Cube`, …). Linked into one image, that is an ODR violation.
- **Size.** The shared core is about 250 KB per build and each plugin adds 30–45 KB. WiFi, the async server and OTA add roughly 600–700 KB. The default 1.25 MB app slot is too tight.
- **Network.** The sign lives on one fixed WiFi network.
- **Hardware access.** The sign is not on the bench right now, so everything except the flicker check must be verifiable by compiling and with a bare ESP32.

## Goals / Non-Goals

**Goals:**
- Pick the running plugin from a phone or PC browser; no reflash to switch.
- Keep "drop one `.cpp` in `src/plugins/`" as the whole authoring story.
- Per-plugin settings editable from the page (scrolling text first).
- Keep solo single-plugin builds working from the same plugin source.
- Over-the-air firmware updates, so adding a plugin doesn't need the USB cable.

**Non-Goals:**
- Loading new plugins at runtime without reflashing (native code can't be loaded dynamically; adding a plugin = rebuild + OTA).
- Resuming a plugin where it left off: switching always restarts.
- Authentication, HTTPS, captive-portal provisioning, AP mode (fixed trusted network; noted as a risk).
- A host-reserved button gesture to cycle plugins (would collide with plugins that use 4–6 buttons; revisit later).
- Moving display scanning into its own task (only if the spike shows flicker — see Decision 4).
- Specs for `button_test` / `row_uniformity` (ported, still unspecced).

## Decisions

### 1. Plugin interface: a descriptor struct plus a self-registering macro

```cpp
// src/host/plugin.h
struct PluginSetting;                       // see Decision 6
struct Plugin {
  const char* id;                           // == filename stem
  const char* label;                        // == // @plugin: text
  void (*start)(ComtelPanel&);              // fresh start: reset ALL state, pinMode, arm input
  void (*tick)(ComtelPanel&);               // was loop() minus panel.service(); must not block
  void (*stop)();                           // optional (nullptr): release anything held
  const PluginSetting* settings;            // optional (nullptr) + count
  uint8_t settingCount;
};
#define COMTEL_PLUGIN(...)  /* static registrar: appends to host registry */
```

A plugin file becomes:

```cpp
// @plugin: Bouncing 2x2 square
#include "../host/plugin.h"
namespace {                     // everything file-local
  int x, y, dx, dy; uint32_t last;
  void start(ComtelPanel& p) { /* re-randomise, fill state */ }
  void tick (ComtelPanel& p) { /* step + p.show() */ }
}
COMTEL_PLUGIN(bouncer, "Bouncing 2x2 square", start, tick, nullptr);
```

- **Function pointers, not virtual classes.** Simpler for contributors to copy, no heap, and the descriptor can be `const`.
- **The registry is a function-local static list** (Meyers singleton), so registration order across translation units doesn't matter. Plugins are listed sorted by id.
- **Id and label are repeated in the macro.** The flasher still parses `// @plugin:` for its menu; the macro is the runtime source. A build-time check that each plugin file's id matches its filename stem is a flasher lint (task), not a runtime check.
- **Anonymous namespace is mandatory.** Plugin code was already file-`static`; the namespace also covers types (`struct Btn`), which `static` can't. This is the ODR fix.

*Alternative — keep `setup()`/`loop()` and rename them with the preprocessor (`-Dsetup=bouncer_setup`):* rejected. It needs per-file build flags PlatformIO can't express in a single env, and it hides the contract.

*Alternative — C++ base class with virtuals:* workable, but heavier for no gain here.

### 2. The host owns the loop, the panel, Serial, and the switch

```
setup():  Serial.begin(115200); panel.begin(); load "host/active" from NVS;
          [launcher] net::begin(); start(active or first plugin)

loop():   panel.service();                 // always first, every pass
          drain command queue (switch / setting)  ── applied here, core 1
          active->tick(panel);
          [launcher] ArduinoOTA.handle();  // cheap when idle
```

Switching (`host::run(id)`):
1. `active->stop()` (if any).
2. Reset the driver calibration fields to their defaults (`rowShiftLo`, `clockKHz`, `rowHoldUs`, `interBurstMs`, `burstClocks`, `scanDuringBurst`, `latchSettleUs`). This undoes `row_uniformity` / `!shift`.
3. `canvas.fillScreen(COMTEL_OFF); show()`.
4. Save the id to NVS (`host` namespace, key `active`) — only when it changed (flash writes stall the scan; see Risks).
5. `next->start(panel)`. This must be a full fresh start: every piece of state re-initialised, `pinMode` set, input re-armed.

The host never reads Serial input; the active plugin owns it (`scrolling_text`'s console, `row_uniformity`'s keys). The host only prints one-line status messages (boot, IP, switches).

**Solo builds** compile the same host with `COMTEL_WEB=0`: no WiFi, web or OTA code, and no NVS restore. Exactly one plugin is registered, and it is started at boot.

### 3. Build layout: one launcher env plus generated solo envs

The generated block in `platformio.ini` becomes:

```ini
[comtel_base]
build_src_filter = -<*> +<driver/> +<host/>
lib_deps = adafruit/Adafruit GFX Library
board_build.partitions = min_spiffs.csv

[env:launcher]
extends = comtel_base
build_src_filter = ${comtel_base.build_src_filter} +<plugins/>
build_flags = -DCOMTEL_WEB=1
lib_deps = ${comtel_base.lib_deps}
  mathieucarbou/ESPAsyncWebServer
  bblanchon/ArduinoJson

[env:bouncer]
extends = comtel_base
build_src_filter = ${comtel_base.build_src_filter} +<plugins/bouncer.cpp>
build_flags = -DCOMTEL_WEB=0
```

The web code lives in `src/host/web*.cpp`, entirely inside `#if COMTEL_WEB`, so solo envs don't link the async libraries. `[env:diag]` is untouched.

`min_spiffs.csv` keeps two OTA app slots of 1.9 MB each. That leaves plenty of room for many more plugins, and OTA still works. Solo envs also use it, so switching between solo and launcher over USB needs no partition change.

### 4. Display scanning stays cooperative; WiFi runs on core 0

The Arduino `loop()` runs on core 1. The WiFi stack, the AsyncTCP task (pinned to core 0) and mDNS all run on core 0, so the main loop's scan rhythm is only affected by:
- WiFi interrupts that land on core 1
- flash-cache stalls (NVS writes, OTA writes)

We keep `service()` on the main loop, so the `comtel-panel` contract is unchanged, and **measure first**. The spike task shows a full-white frame and the `row_uniformity` card while hammering the web API.

If flicker is visible, the escalation path is, in order:
1. Make sure `WiFi.begin()` happens such that its ISR is allocated on core 0. This is a known knob: call it from a task pinned to core 0.
2. Move scanning into a high-priority task pinned to core 1, with a mutex around `show()`. That would be a `comtel-panel` change, made in a follow-up.

*Alternative — move the scan to a task now:* rejected. It adds a mutex to every `show()` and changes the driver contract for a problem we haven't observed.

### 5. Web server: async, and handlers never touch the panel

The server is `mathieucarbou/ESPAsyncWebServer`, the maintained fork, built on AsyncTCP. It runs on core 0, so serving a page never steals time from the scan loop on core 1.

The rule: **request handlers never call the plugin or the panel.**
- A handler validates its input, pushes a `Command { RUN | SET, pluginIdx, settingIdx, value }` onto a FreeRTOS queue (depth 8; if the queue is full, it returns 503) and returns `202 Accepted`.
- The host loop drains the queue between `tick()` calls.
- `GET /api/state` reads a snapshot that the host loop republishes after every change, guarded by a small mutex. Handlers therefore never call into plugin code on core 0.

The API:

| Method / path | Body | Effect |
|---|---|---|
| `GET /` | — | embedded control page (gzipped, in PROGMEM) |
| `GET /api/state` | — | `{active, plugins:[{id,label,settings:[{key,label,type,min,max,options,value}]}]}` |
| `POST /api/run` | `{"id":"tetris"}` | queue a switch; 404 for an unknown id |
| `POST /api/setting` | `{"id":"scrolling_text","key":"message","value":"HI"}` | queue a set; 400 if invalid, 409 if that plugin isn't active |

Settings can only be changed on the active plugin. Their values live in plugin state that is only valid while the plugin runs. It also keeps the model simple.

The page is one small HTML/JS file. It lists the plugins (a tap runs one), renders the active plugin's settings as a form, and polls `/api/state` every few seconds. It is stored as a gzipped byte array generated from `src/host/web/index.html` by a tiny `tools/embed_web.py`, run by hand and committed, so there is no extra build step. No LittleFS is needed.

### 6. Plugin settings: a typed, static schema plus get/set callbacks

```cpp
enum class SettingType : uint8_t { Text, Int, Bool, Choice };
struct PluginSetting {
  const char* key; const char* label; SettingType type;
  int32_t min, max;             // Int range; Text max length in max
  const char* options;          // Choice: "1|2" style, pipe-separated
  void (*get)(char* out, size_t n);       // current value as text
  bool (*set)(const char* value);         // false = rejected
};
```

- **Values travel as strings.** The host checks type, range, length and choice membership before queuing. The plugin's `set` validates again and returns `false` to reject.
- **Persistence belongs to the plugin.** `scrolling_text` already saves to NVS after each change, and keeps doing so from `set`.
- `scrolling_text` declares: `message` (Text, ≤127, edits the current slot), `slot` (Choice 0..3), `speed` (Int 5..2000), `size` (Choice 1|2), `bold` (Bool), `italic` (Bool).

*Alternative — free-form JSON handed to the plugin:* rejected. Every plugin would need its own parser, and the UI couldn't render a form generically.

### 7. WiFi, mDNS and secrets

- `include/secrets.h` (gitignored) defines `COMTEL_WIFI_SSID`, `COMTEL_WIFI_PASS` and `COMTEL_HOSTNAME`, with `COMTEL_OTA_PASS` optional. `include/secrets.example.h` is committed.
- A launcher build without `secrets.h` fails with an `#error` explaining how to create it, rather than shipping unreachable firmware.
- The sign joins as a station with auto-reconnect. If the network is unreachable it keeps running its plugin and retries in the background; WiFi never blocks the boot.
- mDNS advertises `<hostname>.local` with an `_http._tcp` service. The IP address is printed on Serial at connect. For a stable address, use a DHCP reservation on the router (not something the firmware handles).

### 8. OTA updates

ArduinoOTA runs in the launcher build, protected by a password if `COMTEL_OTA_PASS` is set. While an update is writing, the display freezes; that's acceptable.

`tools/flash.py --ota [host]` uploads with `--upload-port <host>` (defaulting to `<hostname>.local` read from `secrets.h`). Each env sets `upload_protocol = espota` only when invoked with OTA. The flasher passes `-t upload --upload-port` together with the espota protocol through the `PLATFORMIO_UPLOAD_FLAGS` environment variable or a `[env:launcher_ota]` extending env — the implementation picks whichever works with the installed PlatformIO. The first flash of the launcher must be over USB.

### 9. Porting the six plugins

Each plugin port follows the same mechanical recipe:
1. Wrap all of the file's code in `namespace {}`.
2. Remove `static ComtelPanel panel;` and use the `ComtelPanel&` passed in.
3. `setup()` becomes `start()`: drop `Serial.begin`/`panel.begin`, and re-initialise *every* state variable explicitly, since static initialisers only ran once at boot.
4. `loop()` becomes `tick()`: drop the leading `panel.service()`.
5. Add `COMTEL_PLUGIN(...)`.

Behaviour must not otherwise change. Specs that describe "at boot" now mean "at start".

## Risks / Trade-offs

- **[WiFi/flash activity makes the CPU-scanned display flicker]** → The bench spike comes before anything else is verified on glass; there is a documented escalation path (Decision 4). The page polls every few seconds, not continuously.
- **[NVS writes stall both cores for a few ms, so a visible blip]** → The host writes `active` only on an actual change. `scrolling_text` already writes per change; the web UI only sends a setting on "Apply", not per keystroke.
- **[A plugin forgets to reset some state in `start()`, so a "fresh" restart is stale]** → The porting recipe calls this out, and every plugin's spec gains a "switching away and back starts fresh" scenario. Review each port against it.
- **[A plugin's `tick()` blocks, starving the scan and the command queue]** → Plugins already had this contract with `loop()`; the interface comment and README state it.
- **[An open, unauthenticated control page on the LAN]** → Accepted for a fixed trusted network; an optional OTA password exists. Revisit before the sign goes anywhere public.
- **[Bigger image, longer USB flashes]** → OTA makes routine updates cable-free; `min_spiffs` leaves room.
- **[The ESPAsyncWebServer fork drifts or breaks with the Arduino core version]** → Pin the library version in `lib_deps`.
- **[Solo envs regress, since nobody uses them day to day]** → `pio run` for every env stays a task in each change; the flasher's compile check builds them.

## Migration Plan

1. Add `src/host/` (interface, registry, host loop) with `COMTEL_WEB=0` only, and port `bouncer`. Check that the solo `bouncer` env compiles.
2. Port the other five plugins; all solo envs compile.
3. Add the launcher env and the flasher changes; the launcher compiles with every plugin.
4. Add networking: secrets, WiFi, mDNS, async server, API, the embedded page, OTA.
5. Bare-ESP32 check (no sign needed): the board joins WiFi, the page loads, switching changes what Serial reports, settings round-trip, OTA works.
6. Bench session with the sign: flicker spike, then the three deferred hardware checks and per-plugin smoke tests.

**Rollback:** everything is on the `web-launcher` branch, and `main` still has the per-plugin sketches.

## Open Questions

- **Hostname default** (`comtel`? `sign`?). This lives in `secrets.h`; the example uses `comtel`.
- **Should the page also show the Serial status lines** (e.g. the Tetris score) via a small log endpoint? Deferred; easy to add later.
- **OTA mechanics with the installed PlatformIO version** (extra env vs upload flags): *resolved* — neither. `flash.py --ota` builds `[env:launcher]` normally (`pio run -e launcher`), then runs the framework's own `espota.py` (`~/.platformio/packages/framework-arduinoespressif32/tools/espota.py -i <host> -f .pio/build/launcher/firmware.bin [-a <COMTEL_OTA_PASS>]`) with the current Python. No extra env (no second build directory) and no reliance on PlatformIO's IP/`.local` auto-switch to espota, which only matches some host forms. `--ota` is refused for solo targets: they have no OTA receiver, so flashing one over the air would strand the board on USB.
