// ============================================================================
//  plugin.h -- the plugin-host interface. Every src/plugins/<id>.cpp includes
//  this and nothing else from the host.
//
//  A plugin is a descriptor (id, label, start, tick, optional stop, optional
//  settings) registered with COMTEL_PLUGIN. The host owns setup()/loop(), the
//  one ComtelPanel and Serial.begin(); a plugin never defines setup()/loop()
//  and never constructs a panel. Template:
//
//    // @plugin: Bouncing 2x2 square
//    #include "../host/plugin.h"
//    namespace {                    // ALL file-local code, types included
//    int x, y;
//    void start(ComtelPanel &panel) { x = 0; y = 0; ... }
//    void tick(ComtelPanel &panel)  { ...; panel.show(); }
//    }  // namespace
//    COMTEL_PLUGIN(bouncer, "Bouncing 2x2 square", start, tick, nullptr);
//
//  CONTRACT
//    start(panel)  A FRESH START, every time: re-initialise every piece of
//                  state (static initialisers only ran once, at boot), set
//                  pin modes, re-arm input. Called at boot and on every
//                  switch to this plugin, including re-selecting it.
//    tick(panel)   The old loop() minus panel.service(): the host services
//                  the display before every tick. Must return promptly.
//    stop()        Optional (nullptr). Release anything start() acquired.
//    settings      Optional: a static const PluginSetting array, passed as
//                  the last macro argument. Values travel as text; the host
//                  validates type/range/length before calling set(), and
//                  set() may still reject by returning false (leaving state
//                  unchanged). Bool values reach set() as "0" or "1".
//                  get()/set() run on the main loop, only while the plugin
//                  is active. Persisting a setting is the plugin's job.
//
//  The id passed to COMTEL_PLUGIN is a bare identifier and must equal the
//  filename stem; the label must match the `// @plugin:` comment (the
//  flasher lints both).
// ============================================================================
#pragma once
#include <Arduino.h>
#include "../driver/comtel_panel.h"

enum class SettingType : uint8_t { Text, Int, Bool, Choice };

struct PluginSetting {
  const char *key;      // unique within the plugin
  const char *label;
  SettingType type;
  int32_t min, max;     // Int: inclusive range. Text: max = max length (<=127)
  const char *options;  // Choice: pipe-separated, e.g. "1|2"; else nullptr
  void (*get)(char *out, size_t n);  // current value as text
  bool (*set)(const char *value);    // false = rejected, state unchanged
};

struct Plugin {
  const char *id;                    // == filename stem
  const char *label;                 // == `// @plugin:` text
  void (*start)(ComtelPanel &);
  void (*tick)(ComtelPanel &);
  void (*stop)();                    // optional (nullptr)
  const PluginSetting *settings;     // optional (nullptr)
  uint8_t settingCount;
};

namespace host {
namespace registry { void add(const Plugin *p); }

// Static registrar: one per plugin file, runs before setup(). The registry is
// a function-local static, so static-initialisation order doesn't matter.
struct Registrar {
  explicit Registrar(const Plugin &p) { registry::add(&p); }
};

namespace detail {
struct SettingList { const PluginSetting *p; uint8_t n; };
constexpr SettingList settingList() { return {nullptr, 0}; }
template <size_t N>
constexpr SettingList settingList(const PluginSetting (&a)[N]) {
  return {a, (uint8_t)N};
}
}  // namespace detail
}  // namespace host

// COMTEL_PLUGIN(id, "label", start, tick, stop[, settings])
#define COMTEL_PLUGIN(id_, label_, start_, tick_, stop_, ...)                \
  static const ::Plugin comtel_plugin_##id_ = {                              \
      #id_, label_, start_, tick_, stop_,                                    \
      ::host::detail::settingList(__VA_ARGS__).p,                            \
      ::host::detail::settingList(__VA_ARGS__).n};                           \
  static ::host::Registrar comtel_registrar_##id_(comtel_plugin_##id_)
