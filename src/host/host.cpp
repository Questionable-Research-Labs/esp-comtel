// ============================================================================
//  host.cpp -- the plugin host: the only Arduino setup()/loop(), the one
//  ComtelPanel, and the active-plugin lifecycle.
//
//    loop():  panel.service()  -> apply queued commands -> active->tick()
//             [launcher] net::loop()
//
//  Switching (run): stop the current plugin, reset the driver calibration and
//  the canvas drawing state, blank the glass, [launcher] remember the id in
//  NVS, then start the new plugin fresh. Nothing resumes.
//
//  The host never reads Serial input -- the active plugin owns it. It only
//  prints one-line "host: ..." status messages.
// ============================================================================
#include "host.h"
#if COMTEL_WEB
#include <Preferences.h>
#endif

namespace {

ComtelPanel panel;
int active = -1;                  // registry index of the running plugin
int pausedPlugin = -1;            // stopped for an OTA transfer (pauseForUpdate)

// ---- calibration reset -----------------------------------------------------
// Plugins may retune the driver (row_uniformity's burst A/B, scrolling_text's
// !shift). Every switch puts it back. The defaults are captured from the
// host's own panel before anything can touch it, so they're the driver's
// in-class initialisers and are never written down a second time here.
// Adding a calibration field to ComtelPanel means adding it to this struct.
struct Calibration {
  int      rowShiftLo;
  uint32_t clockKHz;
  int      rowHoldUs;
  uint32_t interBurstMs;
  uint32_t burstClocks;
  bool     scanDuringBurst;
  uint32_t latchSettleUs;

  static Calibration of(const ComtelPanel &p) {
    return {p.rowShiftLo, p.clockKHz, p.rowHoldUs, p.interBurstMs,
            p.burstClocks, p.scanDuringBurst, p.latchSettleUs};
  }
  void applyTo(ComtelPanel &p) const {
    p.rowShiftLo = rowShiftLo;       p.clockKHz = clockKHz;
    p.rowHoldUs = rowHoldUs;         p.interBurstMs = interBurstMs;
    p.burstClocks = burstClocks;     p.scanDuringBurst = scanDuringBurst;
    p.latchSettleUs = latchSettleUs;
  }
};
Calibration driverDefaults;

// The canvas's GFX drawing state (text size/colour/wrap, cursor, font,
// rotation) leaks between plugins just like calibration; restore the
// GFXcanvas8 constructor's values.
void resetCanvasState(GFXcanvas8 &c) {
  c.setRotation(0);
  c.setFont(nullptr);
  c.setTextSize(1);
  c.setTextColor(0xFFFF);           // Adafruit_GFX ctor: textcolor = textbgcolor
  c.setTextWrap(true);
  c.setCursor(0, 0);
  c.cp437(false);
}

// ---- command queue + snapshot ----------------------------------------------
QueueHandle_t queue = nullptr;
SemaphoreHandle_t snapMutex = nullptr;
host::Snapshot snap;
host::Snapshot scratch;           // built outside the lock, then copied in

void publish() {
  scratch.active = active;
  scratch.nValues = 0;
  if (active >= 0) {
    const Plugin *p = host::registry::at(active);
    for (int i = 0; i < p->settingCount; i++) {
      scratch.values[i][0] = 0;
      p->settings[i].get(scratch.values[i], host::registry::VALUE_MAX);
      scratch.values[i][host::registry::VALUE_MAX - 1] = 0;
    }
    scratch.nValues = p->settingCount;
  }
  xSemaphoreTake(snapMutex, portMAX_DELAY);
  snap = scratch;
  xSemaphoreGive(snapMutex);
}

// ---- persistence of the active plugin (launcher only) ----------------------
#if COMTEL_WEB
char savedId[32] = "";            // what NVS holds, so unchanged ids aren't rewritten

void loadSavedId() {
  Preferences prefs;
  prefs.begin("host", true);
  String v = prefs.getString("active", "");
  prefs.end();
  strlcpy(savedId, v.c_str(), sizeof(savedId));
}

void saveActiveId(const char *id) {
  if (!strcmp(savedId, id)) return;   // flash writes stall the scan: only on change
  Preferences prefs;
  prefs.begin("host", false);
  prefs.putString("active", id);
  prefs.end();
  strlcpy(savedId, id, sizeof(savedId));
}
#endif

// ---- the switch ------------------------------------------------------------
void run(int idx) {
  const Plugin *next = host::registry::at(idx);
  if (!next) return;
  if (active >= 0) {
    const Plugin *cur = host::registry::at(active);
    if (cur->stop) cur->stop();
  }
  driverDefaults.applyTo(panel);
  resetCanvasState(panel.canvas);
  panel.canvas.fillScreen(COMTEL_OFF);
  panel.show();
  active = idx;
#if COMTEL_WEB
  saveActiveId(next->id);
#endif
  Serial.printf("host: start %s (%s)\n", next->id, next->label);
  next->start(panel);
  publish();
}

void apply(const host::Command &c) {
  if (c.type == host::CmdType::Run) {
    run(c.plugin);
    return;
  }
  // Set: re-check activeness -- a switch may have been queued ahead of it.
  if (c.plugin != active) {
    Serial.println(F("host: setting dropped, its plugin is no longer active"));
    return;
  }
  const Plugin *p = host::registry::at(active);
  const PluginSetting &s = p->settings[c.setting];
  bool ok = s.set(c.value);
  Serial.printf("host: %s.%s = \"%s\"%s\n", p->id, s.key, c.value,
                ok ? "" : " rejected by plugin");
  publish();
}

int bootPlugin() {
#if COMTEL_WEB
  loadSavedId();
  if (savedId[0]) {
    int i = host::registry::indexOf(savedId);
    if (i >= 0) return i;
    Serial.printf("host: saved plugin \"%s\" not in this firmware; "
                  "starting the first plugin\n", savedId);
  }
#endif
  return 0;
}

}  // namespace

// ---- host API (host.h) -----------------------------------------------------
namespace host {

bool enqueue(const Command &c) {
  return queue && xQueueSend(queue, &c, 0) == pdTRUE;
}

void readSnapshot(Snapshot &out) {
  xSemaphoreTake(snapMutex, portMAX_DELAY);
  out = snap;
  xSemaphoreGive(snapMutex);
}

const char *validate(const PluginSetting &s, const char *v, char *out, size_t n) {
  switch (s.type) {
    case SettingType::Int: {
      char *end = nullptr;
      long x = strtol(v, &end, 10);
      if (!*v || *end) return "not an integer";
      if (x < s.min || x > s.max) return "out of range";
      snprintf(out, n, "%ld", x);
      return nullptr;
    }
    case SettingType::Bool:
      if (!strcmp(v, "1") || !strcmp(v, "true"))  { strlcpy(out, "1", n); return nullptr; }
      if (!strcmp(v, "0") || !strcmp(v, "false")) { strlcpy(out, "0", n); return nullptr; }
      return "not a boolean";
    case SettingType::Choice: {
      size_t len = strlen(v);
      for (const char *o = s.options; *o;) {
        const char *bar = strchr(o, '|');
        size_t olen = bar ? (size_t)(bar - o) : strlen(o);
        if (olen == len && !strncmp(o, v, len)) { strlcpy(out, v, n); return nullptr; }
        if (!bar) break;
        o = bar + 1;
      }
      return "not one of the options";
    }
    case SettingType::Text:
      if (strlen(v) > (size_t)s.max) return "too long";
      strlcpy(out, v, n);
      return nullptr;
  }
  return "unknown type";
}

void pauseForUpdate() {
  if (active < 0) return;
  const Plugin *cur = registry::at(active);
  if (cur->stop) cur->stop();
  pausedPlugin = active;
  active = -1;
  panel.canvas.fillScreen(COMTEL_OFF);
  panel.show();
}

void resumeAfterUpdate() {
  if (pausedPlugin >= 0) run(pausedPlugin);
  pausedPlugin = -1;
}

}  // namespace host

// ---- Arduino entry points --------------------------------------------------
void setup() {
  driverDefaults = Calibration::of(panel);   // before anything can retune it
  Serial.begin(115200);
  panel.begin();
  queue = xQueueCreate(host::QUEUE_DEPTH, sizeof(host::Command));
  snapMutex = xSemaphoreCreateMutex();

  Serial.printf("\nhost: %d plugin(s), %s build\n", host::registry::count(),
                COMTEL_WEB ? "launcher" : "solo");
  host::registry::reportErrors();
#if COMTEL_WEB
  net::begin();                              // non-blocking; joins in background
#endif
  if (host::registry::count() == 0) {
    Serial.println(F("host: ERROR no plugins registered"));
    publish();
    return;
  }
  run(bootPlugin());
}

void loop() {
  panel.service();                           // always first, every pass
  host::Command c;
  while (xQueueReceive(queue, &c, 0) == pdTRUE) apply(c);
  if (active >= 0) host::registry::at(active)->tick(panel);
#if COMTEL_WEB
  net::loop();
#endif
}
