## ADDED Requirements

### Requirement: WiFi from local secrets

The launcher build SHALL read `COMTEL_WIFI_SSID`, `COMTEL_WIFI_PASS` and `COMTEL_HOSTNAME`, plus an optional `COMTEL_OTA_PASS`, from `include/secrets.h`. That file SHALL be gitignored. `include/secrets.example.h` SHALL be committed as a template. Building the launcher without `secrets.h` SHALL fail with an error explaining how to create it.

#### Scenario: Missing secrets
- **WHEN** the launcher env is built and `include/secrets.h` does not exist
- **THEN** compilation fails with a message telling the user to copy `secrets.example.h`

### Requirement: Non-blocking station connection

The launcher SHALL join the configured network as a WiFi station with automatic reconnection. Connecting SHALL NOT delay starting the active plugin or block the main loop. On connect, it SHALL print the IP address and `http://<hostname>.local` on Serial.

#### Scenario: Network unavailable
- **WHEN** the sign boots with the access point out of range
- **THEN** the saved plugin runs normally, and the sign joins automatically once the network appears

### Requirement: mDNS name

When connected, the launcher SHALL advertise `<hostname>.local` with an `_http._tcp` service on port 80.

#### Scenario: Reach by name
- **WHEN** a device on the same network opens `http://comtel.local` (hostname `comtel`)
- **THEN** the control page loads

### Requirement: JSON API

An asynchronous HTTP server SHALL run off the main loop and serve:
- `GET /api/state` returns `{active, plugins:[{id, label, settings:[{key, label, type, min, max, options, value}]}]}` from the host's snapshot. `value` is present only for the active plugin.
- `POST /api/run` with `{"id": "<id>"}` queues a switch and returns 202, or 404 for an unknown id.
- `POST /api/setting` with `{"id", "key", "value"}` queues a setting and returns 202. It returns 400 for an invalid value or unknown key, and 409 if the plugin is not active.
- Any request returns 503 if the command queue is full.

Request handlers SHALL NOT call plugin or panel code directly.

#### Scenario: Switch via API
- **WHEN** a client posts `{"id":"tetris"}` to `/api/run`
- **THEN** the response is 202, and within one loop pass `tetris` is active, with `/api/state` reporting `active: "tetris"`

#### Scenario: Bad setting
- **WHEN** a client posts `speed = "fast"` for `scrolling_text`
- **THEN** the response is 400, and nothing changes

### Requirement: Embedded control page

`GET /` SHALL serve a single self-contained page, stored in flash with no filesystem, that works on a phone-width screen. The page SHALL:
- list every plugin by label and highlight the active one; selecting a plugin switches to it
- render a form for the active plugin's settings, choosing the input type by setting type (text box, number with bounds, checkbox, choice selector)
- send a value only when the user applies it
- refresh its state from `/api/state` periodically, and after each action
- show errors returned by the API

#### Scenario: Change the scrolling message from a phone
- **WHEN** the user opens the page, taps "Scrolling text", types `HELLO` in Message and applies
- **THEN** the sign scrolls `HELLO`

#### Scenario: Plugin without settings
- **WHEN** the active plugin declares no settings
- **THEN** the page shows no settings form for it

### Requirement: Over-the-air updates

The launcher SHALL accept ArduinoOTA firmware uploads while connected. If `COMTEL_OTA_PASS` is defined, the upload SHALL require that password. After a successful upload the sign SHALL reboot into the new firmware and restore the saved active plugin.

#### Scenario: OTA update
- **WHEN** a new launcher image is uploaded over WiFi
- **THEN** the sign reboots into it and resumes the previously active plugin (fresh start)
