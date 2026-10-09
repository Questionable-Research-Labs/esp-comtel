// Plugin registry -- see registry.h. Registration runs during static
// initialisation, before Serial exists, so problems are recorded here and
// printed by reportErrors() from setup().
#include "registry.h"

namespace host {
namespace registry {

namespace {
struct Store {
  const Plugin *list[MAX_PLUGINS];
  int n = 0;
  static const int MAX_REJECTS = 8;
  const Plugin *rejected[MAX_REJECTS];
  const char *why[MAX_REJECTS];
  int nRejected = 0;
};

// Function-local static: constructed on first use, whichever file's
// registrar gets there first.
Store &store() {
  static Store s;
  return s;
}

void reject(Store &s, const Plugin *p, const char *why) {
  if (s.nRejected < Store::MAX_REJECTS) {
    s.rejected[s.nRejected] = p;
    s.why[s.nRejected] = why;
  }
  s.nRejected++;
}

bool settingsValid(const Plugin *p) {
  if (p->settingCount > MAX_SETTINGS) return false;
  for (int i = 0; i < p->settingCount; i++) {
    const PluginSetting &s = p->settings[i];
    if (!s.key || !s.get || !s.set) return false;
    if (s.type == SettingType::Text && (s.max < 1 || s.max > VALUE_MAX - 1))
      return false;
    if (s.type == SettingType::Choice && !s.options) return false;
  }
  return true;
}
}  // namespace

void add(const Plugin *p) {
  Store &s = store();
  if (!p->id || !p->start || !p->tick) { reject(s, p, "incomplete descriptor"); return; }
  if (!settingsValid(p)) { reject(s, p, "invalid settings"); return; }
  if (s.n >= MAX_PLUGINS) { reject(s, p, "too many plugins"); return; }

  // Insertion sort by id; equal id = duplicate, first one wins.
  int at = 0;
  while (at < s.n) {
    int c = strcmp(p->id, s.list[at]->id);
    if (c == 0) { reject(s, p, "duplicate id"); return; }
    if (c < 0) break;
    at++;
  }
  for (int i = s.n; i > at; i--) s.list[i] = s.list[i - 1];
  s.list[at] = p;
  s.n++;
}

int count() { return store().n; }

const Plugin *at(int i) {
  Store &s = store();
  return (i >= 0 && i < s.n) ? s.list[i] : nullptr;
}

int indexOf(const char *id) {
  Store &s = store();
  for (int i = 0; i < s.n; i++)
    if (!strcmp(s.list[i]->id, id)) return i;
  return -1;
}

void reportErrors() {
  Store &s = store();
  int shown = s.nRejected < Store::MAX_REJECTS ? s.nRejected : Store::MAX_REJECTS;
  for (int i = 0; i < shown; i++)
    Serial.printf("host: ERROR plugin \"%s\" ignored: %s\n",
                  s.rejected[i]->id ? s.rejected[i]->id : "?", s.why[i]);
  if (s.nRejected > shown)
    Serial.printf("host: ERROR %d more plugin(s) ignored\n", s.nRejected - shown);
}

}  // namespace registry
}  // namespace host
