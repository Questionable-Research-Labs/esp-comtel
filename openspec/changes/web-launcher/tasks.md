## 1. Host core (solo mode first)

- [x] 1.1 Create `src/host/plugin.h`: `SettingType`, `PluginSetting`, `Plugin` descriptor, and the `COMTEL_PLUGIN(id, label, start, tick, stop, settings)` macro (static registrar object; settings optional)
- [x] 1.2 Create `src/host/registry.*`: a function-local static registry, sorted by id, with duplicate ids reported on Serial and ignored; lookup by id
- [x] 1.3 Create `src/host/host.cpp`: `setup()` (Serial, `panel.begin()`, start plugin) and `loop()` (`service()`, drain commands, `tick()`); `host::run(id)` doing stop → reset calibration defaults → blank → start; `COMTEL_WEB` defaults to 0
- [x] 1.4 Calibration reset: one helper that restores every `ComtelPanel` calibration field to the driver's defaults (copied from a default-constructed panel's values, not hard-coded twice)
- [x] 1.5 Command queue (FreeRTOS, depth 8) plus a mutex-guarded state snapshot (active id, per-plugin setting values) republished after each applied command

## 2. Port plugins

- [x] 2.1 Port `bouncer` per design Decision 9 (anonymous namespace, `start`/`tick`, `COMTEL_PLUGIN`); `pio run -e bouncer` compiles with the new solo env (needs 3.1)
- [x] 2.2 Port `tetris`: `start()` fully resets the game state, button state, input arming and flash state
- [x] 2.3 Port `entanglement`: `start()` = `configureButtons` + `enterStart`, with every game field, timer and `stealIdx` reset
- [x] 2.4 Port `scrolling_text`: `start()` = pin modes + `loadSettings()` + `showSlot()` + status line (no `Serial.begin`/help); reset button and line-buffer state
- [x] 2.5 Port `button_test` and `row_uniformity` (keep `row_uniformity`'s calibration writes; the host undoes them on switch)
- [x] 2.6 Review each port against "switching back is a fresh start": list any static initialiser that isn't re-set in `start()` and fix it
  - Review: every port re-sets its statics in `start()` (tetris flash/score/top-out; entanglement cubes, electrons, steals, timers; scrolling_text playlist from compiled defaults before NVS, scroller, buttons, speed preset, line buffer; row_uniformity card/cfg/lockouts; button_test edge state). The host also resets the canvas GFX text/cursor state on every switch, which leaked the same way calibration did.

## 3. Build and flasher

- [x] 3.1 `tools/flash.py`: generate the `[comtel_base]` section, `[env:launcher]` and per-plugin solo envs (design Decision 3); keep `diag` and the out-of-block content untouched
- [x] 3.2 Flasher menu shows `launcher` first; accepts `launcher` as an argument
- [x] 3.3 Flasher lint: warn when a plugin's `COMTEL_PLUGIN` id ≠ its filename stem, or its label ≠ its `// @plugin:` text
- [x] 3.4 Pin library versions for `mathieucarbou/ESPAsyncWebServer` and `bblanchon/ArduinoJson` in the launcher env
- [x] 3.5 All solo envs + `diag` compile; record image sizes
  - Sizes (min_spiffs, 1,966,080 B slot): bouncer 279,593; button_test 279,253; entanglement 292,017; row_uniformity 280,009; scrolling_text 294,113; tetris 281,489. `diag` (default 1,310,720 B slot) 292,125.

## 4. Networking (launcher)

- [x] 4.1 `include/secrets.example.h`; add `include/secrets.h` to `.gitignore`; `#error` with instructions when it's missing in a `COMTEL_WEB=1` build
- [x] 4.2 WiFi station with auto-reconnect, non-blocking; print the IP and `http://<hostname>.local` on connect; mDNS with an `_http._tcp` service
- [x] 4.3 Async server: `GET /api/state` from the snapshot, `POST /api/run`, `POST /api/setting` with host-side validation (status codes per the web-ui spec); handlers only enqueue
- [x] 4.4 Persist the `host/active` id on change only; restore at boot, falling back to the first plugin
- [x] 4.5 ArduinoOTA with an optional password; `ArduinoOTA.handle()` in the loop
- [x] 4.6 `[env:launcher]` compiles with every plugin; record the image size against the 1.9 MB slot
  - `launcher`: 895,641 B of 1,966,080 (45.6%); RAM 59,124 B static. Libraries pinned: ESPAsyncWebServer 3.6.0, AsyncTCP 3.3.2 (also pinned, it is a `^` transitive dep), ArduinoJson 7.4.3; AsyncTCP task pinned to core 0 (`-DCONFIG_ASYNC_TCP_RUNNING_CORE=0`).

## 5. Control page

- [x] 5.1 `src/host/web/index.html`: plugin list (tap to run, active highlighted), generated settings form by type, Apply button, error display, poll `/api/state` every ~3 s; phone-width layout, no external assets
- [x] 5.2 `tools/embed_web.py`: gzip `index.html` into `src/host/web_page.h` (a PROGMEM byte array plus its length); commit the generated header
- [x] 5.3 Serve `GET /` with `Content-Encoding: gzip`

## 6. Plugin settings

- [x] 6.1 `scrolling_text` declares `message`, `slot`, `speed`, `size`, `bold`, `italic` with `get`/`set` mapped to the existing console actions (including saving to NVS; blank message rejected)

## 7. OTA flashing from the PC

- [x] 7.1 `tools/flash.py --ota [host]`: espota upload to the given host, defaulting to `<COMTEL_HOSTNAME>.local` from `secrets.h`; pass the OTA password; non-zero exit on failure. Record the chosen mechanism in design.md Open Questions

## 8. Docs

- [x] 8.1 README: launcher vs solo, first USB flash then OTA, creating `secrets.h`, opening the web page, the new plugin authoring template (`start`/`tick`/`COMTEL_PLUGIN`, anonymous namespace, fresh-start rule, optional settings)
- [x] 8.2 Update `openspec/config.yaml` context: host owns `setup`/`loop`; launcher and solo builds

## 9. Bare-ESP32 checks (no sign needed)

- [ ] 9.1 Launcher boots, joins WiFi, and prints the IP; `http://<hostname>.local` loads the page on a phone
- [ ] 9.2 Switching from the page changes the active plugin (Serial shows the switch); a power cycle restores it
- [ ] 9.3 `scrolling_text` settings round-trip: page → `/api/state` shows the new value; `!status` on Serial agrees; an invalid value is rejected with a visible error
- [ ] 9.4 OTA: `python tools/flash.py launcher --ota` updates the board, which comes back on the same plugin
- [ ] 9.5 WiFi down at boot: the plugin still runs; the board joins when the AP returns

## 10. Bench session with the sign

- [ ] 10.1 Flicker spike: full-white frame and the `row_uniformity` cards while hammering `/api/state` and switching repeatedly; compare against the solo build. If flicker appears, follow the design Decision 4 escalation and record the outcome
- [ ] 10.2 Colour: R, G, B, Y, C, M, W bars, confirming each channel mapping (deferred from `tidy-up` 6.1)
- [ ] 10.3 Tetris: upright play, left/right/rotate, soft drop, DAS, line clears with the serial score, top-out flash and restart (deferred from `tidy-up` 6.2)
- [ ] 10.4 Scrolling text: `!size 1/2`, `!bold`, `!italic`, alone and combined, wrap cleanly; settings survive a power cycle; first boot after erase shows the defaults (deferred from `tidy-up` 6.3)
- [ ] 10.5 Smoke-test every plugin via the page, including switching away and back mid-game (fresh start) and `entanglement`'s four buttons
