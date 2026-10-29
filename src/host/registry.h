// Plugin registry: every COMTEL_PLUGIN in the image, sorted by id.
#pragma once
#include "plugin.h"

namespace host {
namespace registry {

static const int MAX_PLUGINS  = 32;
static const int MAX_SETTINGS = 8;    // per plugin
static const int VALUE_MAX    = 128;  // setting value text, incl. NUL

void add(const Plugin *p);            // called by COMTEL_PLUGIN, pre-setup()
int count();
const Plugin *at(int i);              // ascending id order
int indexOf(const char *id);          // -1 if not registered
void reportErrors();                  // print rejected registrations (setup())

}  // namespace registry
}  // namespace host
