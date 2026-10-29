## Why

Changing what the sign shows means reflashing it over USB from a PC. The sign sits on a fixed WiFi network, so a phone-friendly web page could choose the running plugin instead, and could also change plugin settings such as the scrolling message. This must keep the authoring model people already use: one `.cpp` per plugin, so others can keep adding their own.

## What Changes

- **BREAKING — runtime plugin host.** A new host in `src/host/` owns Arduino `setup()`/`loop()`, the single `ComtelPanel`, WiFi, the web server and OTA updates. Plugins stop defining `setup()`/`loop()`. Instead each one implements `start` / `tick` / `stop` against the host-owned panel and registers itself with a `COMTEL_PLUGIN(...)` macro. Exactly one plugin is active at a time.
- **BREAKING — all plugins in one image.** A new `[env:launcher]` compiles the host and every plugin. Each plugin's file-local code goes in an anonymous namespace so plugins can't collide. "Exactly one plugin per image" stops being a rule for the launcher build.
- **Solo builds kept.** The per-plugin `[env:<id>]` envs remain: host (with WiFi compiled out) plus one plugin, which auto-starts. The plugin file is identical in both builds.
- **Switching restarts.** Selecting a plugin stops the current one, blanks the panel and starts the new one fresh. Nothing resumes. The last selection is saved and restored at boot.
- **Plugin settings.** A plugin may declare typed settings (text, integer range, boolean, choice). The web UI renders them generically and the host applies them on the main loop. `scrolling_text` exposes its message, slot, speed, size, bold and italic.
- **Web UI and JSON API.** A single embedded page, plus `GET /api/state`, `POST /api/run` and `POST /api/setting`, served by an async web server on core 0. Handlers never touch the panel; they queue commands for the host loop.
- **Network.** WiFi credentials and hostname come from a gitignored `include/secrets.h`, with a `secrets.example.h` checked in. The sign is reachable via mDNS at `http://<hostname>.local`. If WiFi is down, the sign keeps running its last plugin.
- **OTA.** The launcher accepts ArduinoOTA uploads. `tools/flash.py` gains `--ota [host]`.
- **Flasher.** `--sync` generates `[env:launcher]` and the per-plugin solo envs, and the flasher menu offers `launcher` first.
- **Partition table.** The launcher and solo builds use `min_spiffs.csv` (two 1.9 MB OTA app slots), so the combined image has room to grow.
- **Hardware verification**, deferred from `tidy-up`: colour bars, Tetris, scrolling text, plus a new WiFi/display-flicker check.

## Capabilities

### New Capabilities
- `plugin-host`: the runtime host — plugin interface and registration, active-plugin lifecycle (start/tick/stop, switch = fresh start), boot restore, command queue between the web server and the main loop, solo vs launcher behaviour.
- `plugin-settings`: how a plugin declares typed settings, how the host reads and applies them, and validation rules.
- `web-ui`: WiFi/mDNS/secrets configuration, the JSON API, the embedded control page, and OTA updates.

### Modified Capabilities
- `plugin-system`: plugins implement the host interface instead of `setup()`/`loop()`; a plugin's code is wrapped in an anonymous namespace; one plugin per image applies only to solo builds; the launcher image holds every plugin.
- `plugin-flasher`: generates `[env:launcher]` plus solo envs (host + one plugin), lists `launcher` first, and supports `--ota`.
- `bouncer`, `tetris`, `entanglement`, `scrolling-text`: entry-point requirements now refer to the host interface, and each start is a fresh start. `scrolling-text` also declares its web settings.

## Impact

- **New code:** `src/host/` (host, registry, plugin interface header, web server, embedded page), `include/secrets.example.h`.
- **Ported:** all six plugins in `src/plugins/` (`button_test` and `row_uniformity` included, even though they have no specs).
- **Build:** `platformio.ini` generated block, `board_build.partitions`; new library dependencies `mathieucarbou/ESPAsyncWebServer` (which pulls in AsyncTCP) and `bblanchon/ArduinoJson`. ArduinoOTA and ESPmDNS ship with the Arduino core.
- **Tooling:** `tools/flash.py` (launcher env, solo envs, `--ota`).
- **Unchanged:** `ComtelPanel` driver and `comtel-panel` spec (scanning stays cooperative on the main loop, so the WiFi spike decides whether that holds), `button_pins.h`, `diag` env.
- **Docs:** README (launcher workflow, secrets, web UI, plugin authoring), `.gitignore` (`include/secrets.h`).
