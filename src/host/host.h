// Host internals shared between the main loop (host.cpp) and the web server
// (web.cpp). Plugins don't include this; they only see plugin.h.
#pragma once
#include "plugin.h"
#include "registry.h"

#ifndef COMTEL_WEB
#define COMTEL_WEB 0      // solo build: no WiFi, web, mDNS, OTA or host NVS
#endif

namespace host {

// A request from outside the main loop. Queued, then applied on the main loop
// between tick()s -- code off the main loop never calls plugin or panel code.
enum class CmdType : uint8_t { Run, Set };
struct Command {
  CmdType type;
  int8_t  plugin;                       // registry index
  int8_t  setting;                      // Set: index into plugin->settings
  char    value[registry::VALUE_MAX];   // Set: already validated text
};
static const int QUEUE_DEPTH = 8;
bool enqueue(const Command &c);         // false = queue full, retry later

// What readers off the main loop may know, republished by the main loop after
// every applied command. Values are the active plugin's settings, in order.
struct Snapshot {
  int     active;                       // registry index, -1 = none
  uint8_t nValues;
  char    values[registry::MAX_SETTINGS][registry::VALUE_MAX];
};
void readSnapshot(Snapshot &out);

// Host-side validation of a setting value. Returns nullptr if acceptable and
// writes the value to pass to set() into `out` (Bool is normalised to "0" or
// "1"); otherwise returns a short reason.
const char *validate(const PluginSetting &s, const char *value,
                     char *out, size_t n);

// An OTA transfer blocks the main loop, so service() stops and the glass
// would sit on one scan address. Main loop only: stop the plugin and blank
// the panel; resume restarts it (fresh) if the update fails.
void pauseForUpdate();
void resumeAfterUpdate();

}  // namespace host

#if COMTEL_WEB
namespace net {
void begin();   // WiFi, then (on first connect) mDNS, web server and OTA
void loop();    // once per main-loop pass; cheap when idle
}  // namespace net
#endif
