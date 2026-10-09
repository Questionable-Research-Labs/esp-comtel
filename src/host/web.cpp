// ============================================================================
//  web.cpp -- launcher networking: WiFi station, mDNS, the control page and
//  JSON API (ESPAsyncWebServer), and ArduinoOTA. Compiled only when
//  COMTEL_WEB=1, so solo builds link none of it.
//
//  THE RULE: request handlers run on the AsyncTCP task (core 0) and never call
//  plugin or panel code. They validate, queue a host::Command, and answer
//  202; the main loop applies it. /api/state reads the host's snapshot.
//
//    GET  /            the control page (gzipped, in flash)
//    GET  /api/state   {active, plugins:[{id,label,settings:[...]}]}
//    POST /api/run     {"id":"tetris"}                 202 | 404 | 503
//    POST /api/setting {"id","key","value"}            202 | 400 | 404 | 409 | 503
// ============================================================================
#include "host.h"
#if COMTEL_WEB

#if !__has_include("secrets.h")
#error "include/secrets.h is missing. Copy include/secrets.example.h to include/secrets.h and fill in your WiFi SSID, password and hostname (the file is gitignored)."
#else
#include "secrets.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "web_page.h"

namespace net {
namespace {

using host::registry::MAX_SETTINGS;
using host::registry::VALUE_MAX;

AsyncWebServer server(80);

// Set by the WiFi event task, consumed by net::loop() on the main loop.
volatile bool gotIp = false;
volatile bool dropped = false;
bool linkUp = false;              // main-loop view, so a drop is reported once
bool servicesUp = false;          // mDNS, HTTP server and OTA, started once

// ---- request helpers -------------------------------------------------------
const size_t MAX_BODY = 512;

// Collect a (small) JSON body into request->_tempObject, which the server
// frees with the request. Oversized bodies are dropped and answered 400.
void collectBody(AsyncWebServerRequest *req, uint8_t *data, size_t len,
                 size_t index, size_t total) {
  if (total > MAX_BODY) return;
  if (index == 0) {
    req->_tempObject = malloc(total + 1);
    if (!req->_tempObject) return;
  }
  if (!req->_tempObject) return;
  memcpy((char *)req->_tempObject + index, data, len);
  if (index + len == total) ((char *)req->_tempObject)[total] = 0;
}

void sendError(AsyncWebServerRequest *req, int code, const char *msg) {
  JsonDocument doc;
  doc["error"] = msg;
  String out;
  serializeJson(doc, out);
  req->send(code, "application/json", out);
}

void sendAccepted(AsyncWebServerRequest *req) {
  req->send(202, "application/json", "{\"ok\":true}");
}

// Parse the collected body; on failure answer 400 and return false.
bool parseBody(AsyncWebServerRequest *req, JsonDocument &doc) {
  if (!req->_tempObject) { sendError(req, 400, "missing or oversized JSON body"); return false; }
  if (deserializeJson(doc, (const char *)req->_tempObject)) {
    sendError(req, 400, "invalid JSON");
    return false;
  }
  return true;
}

const char *typeName(SettingType t) {
  switch (t) {
    case SettingType::Text:   return "text";
    case SettingType::Int:    return "int";
    case SettingType::Bool:   return "bool";
    case SettingType::Choice: return "choice";
  }
  return "?";
}

// ---- handlers --------------------------------------------------------------
host::Snapshot snap;   // handlers all run on the one AsyncTCP task

void handleState(AsyncWebServerRequest *req) {
  host::readSnapshot(snap);
  JsonDocument doc;
  const Plugin *act = host::registry::at(snap.active);
  if (act) doc["active"] = act->id; else doc["active"] = nullptr;
  JsonArray plugins = doc["plugins"].to<JsonArray>();
  for (int i = 0; i < host::registry::count(); i++) {
    const Plugin *p = host::registry::at(i);
    JsonObject jp = plugins.add<JsonObject>();
    jp["id"] = p->id;
    jp["label"] = p->label;
    JsonArray settings = jp["settings"].to<JsonArray>();
    for (int k = 0; k < p->settingCount; k++) {
      const PluginSetting &s = p->settings[k];
      JsonObject js = settings.add<JsonObject>();
      js["key"] = s.key;
      js["label"] = s.label ? s.label : s.key;
      js["type"] = typeName(s.type);
      if (s.type == SettingType::Int) { js["min"] = s.min; js["max"] = s.max; }
      if (s.type == SettingType::Text) js["max"] = s.max;
      if (s.type == SettingType::Choice) {
        JsonArray opts = js["options"].to<JsonArray>();
        String all = s.options;
        int from = 0;
        while (true) {
          int bar = all.indexOf('|', from);
          opts.add(all.substring(from, bar < 0 ? all.length() : bar));
          if (bar < 0) break;
          from = bar + 1;
        }
      }
      if (i == snap.active && k < snap.nValues) js["value"] = snap.values[k];
    }
  }
  AsyncResponseStream *res = req->beginResponseStream("application/json");
  res->addHeader("Cache-Control", "no-store");
  serializeJson(doc, *res);
  req->send(res);
}

void handleRun(AsyncWebServerRequest *req) {
  JsonDocument doc;
  if (!parseBody(req, doc)) return;
  const char *id = doc["id"] | "";
  int idx = host::registry::indexOf(id);
  if (idx < 0) { sendError(req, 404, "unknown plugin"); return; }
  host::Command c{};
  c.type = host::CmdType::Run;
  c.plugin = idx;
  if (!host::enqueue(c)) { sendError(req, 503, "busy, retry"); return; }
  sendAccepted(req);
}

void handleSetting(AsyncWebServerRequest *req) {
  JsonDocument doc;
  if (!parseBody(req, doc)) return;
  const char *id = doc["id"] | "";
  const char *key = doc["key"] | "";
  int idx = host::registry::indexOf(id);
  if (idx < 0) { sendError(req, 404, "unknown plugin"); return; }
  const Plugin *p = host::registry::at(idx);
  int k = 0;
  while (k < p->settingCount && strcmp(p->settings[k].key, key)) k++;
  if (k == p->settingCount) { sendError(req, 400, "unknown setting"); return; }

  // Values travel as text; accept JSON numbers and booleans too.
  char raw[VALUE_MAX];
  JsonVariant v = doc["value"];
  if (v.is<const char *>()) {
    const char *s = v.as<const char *>();
    if (strlen(s) >= sizeof(raw)) { sendError(req, 400, "too long"); return; }
    strlcpy(raw, s, sizeof(raw));
  } else if (v.is<bool>()) {
    strlcpy(raw, v.as<bool>() ? "1" : "0", sizeof(raw));
  } else if (v.is<long>()) {
    snprintf(raw, sizeof(raw), "%ld", v.as<long>());
  } else {
    sendError(req, 400, "missing value");
    return;
  }

  host::Command c{};
  c.type = host::CmdType::Set;
  c.plugin = idx;
  c.setting = k;
  const char *why = host::validate(p->settings[k], raw, c.value, sizeof(c.value));
  if (why) { sendError(req, 400, why); return; }

  host::readSnapshot(snap);
  if (snap.active != idx) { sendError(req, 409, "plugin is not active"); return; }
  if (!host::enqueue(c)) { sendError(req, 503, "busy, retry"); return; }
  sendAccepted(req);
}

void handlePage(AsyncWebServerRequest *req) {
  AsyncWebServerResponse *res =
      req->beginResponse(200, "text/html", WEB_PAGE_GZ, WEB_PAGE_GZ_LEN);
  res->addHeader("Content-Encoding", "gzip");
  req->send(res);
}

// ---- bring-up --------------------------------------------------------------
void startServices() {
  // ArduinoOTA.begin() also starts mDNS under the same hostname.
  ArduinoOTA.setHostname(COMTEL_HOSTNAME);
#ifdef COMTEL_OTA_PASS
  ArduinoOTA.setPassword(COMTEL_OTA_PASS);
#endif
  ArduinoOTA.onStart([]() {
    Serial.println(F("host: OTA update starting (display paused)"));
    host::pauseForUpdate();
  });
  ArduinoOTA.onEnd([]() { Serial.println(F("host: OTA update done, rebooting")); });
  ArduinoOTA.onError([](ota_error_t e) {
    Serial.printf("host: OTA update failed (error %u)\n", (unsigned)e);
    host::resumeAfterUpdate();
  });
  ArduinoOTA.begin();
  MDNS.addService("http", "tcp", 80);

  server.on("/", HTTP_GET, handlePage);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/run", HTTP_POST, handleRun, nullptr, collectBody);
  server.on("/api/setting", HTTP_POST, handleSetting, nullptr, collectBody);
  server.onNotFound([](AsyncWebServerRequest *req) { sendError(req, 404, "not found"); });
  server.begin();
  servicesUp = true;
}

void onWiFiEvent(WiFiEvent_t event) {
  // Runs on the WiFi event task: only set flags (and the core's own
  // auto-reconnect retries after a drop or a missing access point).
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) gotIp = true;
  else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) dropped = true;
}

}  // namespace

void begin() {
  WiFi.onEvent(onWiFiEvent);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(COMTEL_HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(COMTEL_WIFI_SSID, COMTEL_WIFI_PASS);   // returns at once
  Serial.printf("host: joining WiFi \"%s\" in the background\n", COMTEL_WIFI_SSID);
}

void loop() {
  if (dropped) {
    dropped = false;
    if (linkUp) Serial.println(F("host: WiFi lost, reconnecting in the background"));
    linkUp = false;
  }
  if (gotIp) {
    gotIp = false;
    linkUp = true;
    if (!servicesUp) startServices();
    Serial.printf("host: WiFi connected, IP %s -- http://%s.local/\n",
                  WiFi.localIP().toString().c_str(), COMTEL_HOSTNAME);
  }
  if (servicesUp) ArduinoOTA.handle();
}

}  // namespace net

#endif  // secrets.h
#endif  // COMTEL_WEB
