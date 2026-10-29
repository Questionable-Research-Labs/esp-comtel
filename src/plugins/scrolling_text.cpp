// @plugin: Scrolling text with playlist, styles, buttons and NVS
// ============================================================================
//  scrolling_text -- the full scrolling-message app as a self-contained plugin
//  Panel protocol: docs/PROTOCOL.md
//  Depends only on the ComtelPanel driver; the ScrollText renderer is folded in
//  below as file-local code (no shared render library).
//
//  Serial UI @115200, line-based:
//    any text line     -> set current playlist slot + display it
//    !slot <0..3>      -> switch playlist slot
//    !list             -> show playlist
//    !size <1|2>       -> text size: 1 = 6x8, 2 = 12x16 (default)
//    !bold [0|1]       -> bold (bare = toggle)
//    !italic [0|1]     -> italic (bare = toggle)
//    !speed <ms>       -> scroll step interval (default 45)
//    !shift <0..7>     -> lower-half row-decoder offset (session only)
//    !card             -> module-ID test card (six vlines), pauses scrolling
//    !stair / !row     -> row-mapping diagnostics
//    !stop / !go       -> pause / resume scrolling
//    !status, !help
//  Playlist, slot, size, bold, italic and speed persist to NVS across reboots
//  and plugin switches (!shift / clock calibration do not -- the host resets
//  the driver calibration on every switch).
//
//  Web settings (launcher page): message, slot, speed, size, bold, italic --
//  each does exactly what its serial command does, including saving to NVS.
//
//  Buttons (to GND, internal pull-ups):
//    BTN1 = GPIO32: short = next message, long = pause/resume
//    BTN2 = GPIO33: short = cycle speed,  long = test card
// ============================================================================
#include <Arduino.h>
#include <Preferences.h>
#include "../host/plugin.h"
#include "../driver/button_pins.h"

namespace {

// ---- ScrollText renderer (file-local; folded in from the old scroll_text.*) -
// Owns its message, scroll position and pacing; call tick() every pass.
class ScrollText {
 public:
  void attach(ComtelPanel &p) { panel = &p; }   // the host's panel, from start()

  void setMessage(const char *m);   // copies, appends a repeat gap, restarts
  const char *message() const { return msg; }

  void setStepMs(uint32_t ms) { stepMsVal = ms; }
  uint32_t stepMs() const { return stepMsVal; }

  void setRunning(bool on) { run = on; }
  bool running() const { return run; }

  // style options; any change restarts the scroll cleanly
  void setSize(int s)   { if (s == 1 || s == 2) { sz = s; restart(); } }
  int  size() const     { return sz; }
  void setBold(bool b)  { bold = b; restart(); }
  bool boldOn() const   { return bold; }
  void setItalic(bool i){ italic = i; restart(); }
  bool italicOn() const { return italic; }

  void restart() { x = ComtelPanel::W; }
  void tick();                      // renders + panel.show() when a step is due

 private:
  void render();
  void shearItalic();               // in-place row shear on the canvas buffer

  int  glyphW() const { return 6 * sz; }        // 6x8 GFX font scaled by size
  int  textPixelWidth() const {                 // width incl. bold/italic extra
    int w = (int)strlen(msg) * glyphW();
    if (bold)   w += sz;                         // double-strike offset
    if (italic) w += 3;                          // top-row lean
    return w;
  }

  ComtelPanel *panel = nullptr;
  char msg[132] = "";
  int  x = ComtelPanel::W;
  uint32_t stepMsVal = 45, last = 0;
  bool run = false;

  int  sz = 2;                      // 1 = 6x8, 2 = 12x16 (default)
  bool bold = false, italic = false;
};

void ScrollText::setMessage(const char *m) {
  strncpy(msg, m, sizeof(msg) - 4);
  msg[sizeof(msg) - 4] = 0;
  strcat(msg, "   ");               // gap between repeats
  restart();
  run = true;
}

void ScrollText::tick() {
  if (!run || millis() - last < stepMsVal) return;
  last = millis();
  render();
}

// Fake italic: shear the finished canvas so upper rows lean right. Row y is
// shifted right by (H-1-y)/4 px (0..3), bottom row anchored. The GFXcanvas8
// buffer is one byte per pixel (96 bytes/row), so a rightward pixel shift is a
// byte-wise copy; walk right-to-left so we don't overwrite pixels we still
// need, and vacate the leftmost columns with COMTEL_OFF. Color is preserved.
void ScrollText::shearItalic() {
  uint8_t *buf = panel->canvas.getBuffer();
  const int bpr = ComtelPanel::W;               // 96 bytes/row (1 byte/pixel)
  for (int y = 0; y < ComtelPanel::H; y++) {
    int s = (ComtelPanel::H - 1 - y) / 4;       // shift right 0..3 px
    if (!s) continue;
    uint8_t *row = buf + y * bpr;
    for (int x = bpr - 1; x >= 0; x--)
      row[x] = (x >= s) ? row[x - s] : COMTEL_OFF;
  }
}

void ScrollText::render() {
  GFXcanvas8 &c = panel->canvas;
  c.fillScreen(COMTEL_OFF);
  c.setTextWrap(false);
  c.setTextColor(COMTEL_WHITE);     // monochrome white text on the RGB canvas
  c.setTextSize(sz);                // size 1 = 6x8, size 2 = 12x16
  int y = (sz == 1) ? 4 : 0;        // center small text on the 16px panel
  c.setCursor(x, y);
  c.print(msg);
  if (bold) {                       // double-strike offset by size px
    c.setCursor(x + sz, y);
    c.print(msg);
  }
  if (italic) shearItalic();
  panel->show();
  if (--x < -textPixelWidth()) x = ComtelPanel::W;
}

// ---- app --------------------------------------------------------------------
static ComtelPanel *hostPanel = nullptr;   // set by start()
static ScrollText scroller;
static Preferences prefs;           // NVS store, namespace "ledsign"

// ---- message playlist -------------------------------------------------------
static const int NMSG = 4;
static const char DEFAULT_PLAYLIST[NMSG][128] = {   // first-boot contents
    "QUESTIONABLE RESEARCH",
    "HELLO",
    "",
    "",
};
static char playlist[NMSG][128];
static int msgIdx = 0;

static const uint32_t speedPresets[] = {90, 45, 20};  // ms/step: slow/med/fast
static int speedIdx = 1;

// ---- buttons ----------------------------------------------------------------
struct Btn {
  explicit Btn(int p) : pin(p) {}
  int pin;
  bool stable = true;          // debounced level (true = released)
  bool lastRead = true;
  uint32_t tRead = 0, tDown = 0;
  bool longFired = false;
};
static Btn btn1{BTN_PIN_1}, btn2{BTN_PIN_2};

// returns: 0 = nothing, 1 = short press, 2 = long press
static int pollBtn(Btn &b) {
  bool raw = digitalRead(b.pin);
  if (raw != b.lastRead) { b.lastRead = raw; b.tRead = millis(); }
  if (millis() - b.tRead < 30) return 0;         // debounce window
  if (raw == b.stable) {
    if (!raw && !b.longFired && millis() - b.tDown >= 600) {
      b.longFired = true;                        // long fires while held
      return 2;
    }
    return 0;
  }
  b.stable = raw;
  if (!raw) { b.tDown = millis(); b.longFired = false; return 0; }  // pressed
  return b.longFired ? 0 : 1;                    // released: short press
}

// ---- persistence (NVS) ------------------------------------------------------
// Saved: playlist msg0..msg3, current slot, size, bold, ital, speed.
// NOT saved: rowShiftLo / clock calibration (session-only, see !shift).
static void saveSettings() {
  prefs.begin("ledsign", false);                 // read/write
  char key[6];
  for (int i = 0; i < NMSG; i++) {
    snprintf(key, sizeof(key), "msg%d", i);
    prefs.putString(key, playlist[i]);
  }
  prefs.putInt("slot", msgIdx);
  prefs.putInt("size", scroller.size());
  prefs.putInt("bold", scroller.boldOn() ? 1 : 0);
  prefs.putInt("ital", scroller.italicOn() ? 1 : 0);
  prefs.putUInt("speed", scroller.stepMs());
  prefs.end();
}

static void loadSettings() {
  memcpy(playlist, DEFAULT_PLAYLIST, sizeof(playlist));
  prefs.begin("ledsign", true);                   // read-only; missing = default
  char key[6];
  for (int i = 0; i < NMSG; i++) {
    snprintf(key, sizeof(key), "msg%d", i);
    String v = prefs.getString(key, playlist[i]); // compiled-in slot = fallback
    strncpy(playlist[i], v.c_str(), sizeof(playlist[0]) - 1);
    playlist[i][sizeof(playlist[0]) - 1] = 0;
  }
  msgIdx = constrain(prefs.getInt("slot", 0), 0, NMSG - 1);
  scroller.setSize(prefs.getInt("size", 2));
  scroller.setBold(prefs.getInt("bold", 0) != 0);
  scroller.setItalic(prefs.getInt("ital", 0) != 0);
  scroller.setStepMs(prefs.getUInt("speed", 45));
  prefs.end();
  if (!playlist[msgIdx][0]) msgIdx = 0;           // never boot to a blank slot
}

static void showSlot(int n) {
  msgIdx = constrain(n, 0, NMSG - 1);
  scroller.setMessage(playlist[msgIdx]);
}

static void nextMessage() {
  for (int i = 1; i <= NMSG; i++) {              // skip empty slots
    int n = (msgIdx + i) % NMSG;
    if (playlist[n][0]) { showSlot(n); return; }
  }
}

static void testCard() {
  ComtelPanel &panel = *hostPanel;
  scroller.setRunning(false);
  panel.canvas.fillScreen(COMTEL_OFF);
  for (int m = 0; m < 6; m++)
    panel.canvas.drawFastVLine(17 * m, 0, 16, COMTEL_WHITE);
  panel.show();
}

// row-mapping diagnostics: which canvas row feeds which glass row?
static void stairCard() {                      // canvas row y -> cols 6y..6y+5
  ComtelPanel &panel = *hostPanel;
  scroller.setRunning(false);
  panel.canvas.fillScreen(COMTEL_OFF);
  for (int y = 0; y < 16; y++)
    panel.canvas.drawFastHLine(6 * y, y, 6, COMTEL_WHITE);
  panel.show();
  Serial.println(F("staircase: canvas row y lit at cols 6y..6y+5 (y=0 top, col 0 left)"));
}

static void rowProbe(int y) {                  // light one full canvas row
  ComtelPanel &panel = *hostPanel;
  scroller.setRunning(false);
  panel.canvas.fillScreen(COMTEL_OFF);
  panel.canvas.drawFastHLine(0, y, ComtelPanel::W, COMTEL_WHITE);
  panel.show();
  Serial.printf("canvas row %d lit (0=top; !go to resume)\n", y);
}

static void help() {
  Serial.println(F("Comtel sign (scrolling_text): type a message (sets current playlist slot), or:"));
  Serial.println(F("  !slot <0..3>  !list  !speed <ms>  !shift <0..7>"));
  Serial.println(F("  !size <1|2>  !bold [0|1]  !italic [0|1]"));
  Serial.println(F("  !card  !stair  !row <0..15>  !stop  !go  !status"));
  Serial.println(F("buttons: BTN1(GPIO32) short=next msg long=pause/resume"));
  Serial.println(F("         BTN2(GPIO33) short=speed   long=test card"));
  Serial.println(F("message, slot, size, bold, italic and speed persist across reboots"));
}

static void status() {
  Serial.printf("slot=%d msg=\"%s\" stepMs=%lu running=%d size=%d bold=%d italic=%d "
                "rowShiftLo=%d clock=%lukHz\n",
                msgIdx, scroller.message(), (unsigned long)scroller.stepMs(),
                scroller.running(), scroller.size(), scroller.boldOn(),
                scroller.italicOn(), hostPanel->rowShiftLo,
                (unsigned long)hostPanel->clockKHz);
}

// Set the current playlist slot's text and show it (a typed line, or the web
// `message` setting). Leading spaces are dropped; a blank line is ignored.
static bool setCurrentMessage(const char *l) {
  while (*l == ' ') l++;
  if (!*l) return false;
  strncpy(playlist[msgIdx], l, sizeof(playlist[0]) - 1);
  playlist[msgIdx][sizeof(playlist[0]) - 1] = 0;
  showSlot(msgIdx);
  saveSettings();
  Serial.printf("slot %d set: \"%s\"\n", msgIdx, playlist[msgIdx]);
  return true;
}

static void handleLine(char *l) {
  while (*l == ' ') l++;
  if (!*l) return;
  if (*l == '!') {
    if (!strncmp(l, "!speed ", 7))      { scroller.setStepMs(constrain(atoi(l + 7), 5, 2000)); saveSettings(); status(); }
    else if (!strncmp(l, "!shift ", 7)) { hostPanel->rowShiftLo = atoi(l + 7) & 7; status(); }
    else if (!strncmp(l, "!slot ", 6))  { showSlot(atoi(l + 6)); saveSettings(); status(); }
    else if (!strncmp(l, "!size ", 6))  { scroller.setSize(atoi(l + 6)); saveSettings(); status(); }
    else if (!strcmp(l, "!bold"))       { scroller.setBold(!scroller.boldOn()); saveSettings(); status(); }
    else if (!strncmp(l, "!bold ", 6))  { scroller.setBold(atoi(l + 6) != 0); saveSettings(); status(); }
    else if (!strcmp(l, "!italic"))     { scroller.setItalic(!scroller.italicOn()); saveSettings(); status(); }
    else if (!strncmp(l, "!italic ", 8)){ scroller.setItalic(atoi(l + 8) != 0); saveSettings(); status(); }
    else if (!strcmp(l, "!list"))       { for (int i = 0; i < NMSG; i++)
                                            Serial.printf("  %d%s \"%s\"\n", i,
                                                          i == msgIdx ? "*" : " ", playlist[i]); }
    else if (!strcmp(l, "!card"))       { testCard(); Serial.println(F("test card (!go to resume)")); }
    else if (!strcmp(l, "!stair"))      { stairCard(); }
    else if (!strncmp(l, "!row ", 5))   { rowProbe(atoi(l + 5) & 15); }
    else if (!strcmp(l, "!stop"))       { scroller.setRunning(false); Serial.println(F("paused")); }
    else if (!strcmp(l, "!go"))         { scroller.setRunning(true); }
    else if (!strcmp(l, "!status"))     { status(); }
    else help();
  } else {
    setCurrentMessage(l);
  }
}

// Serial line assembly, reset by start() so a half-typed line doesn't leak in.
static char line[130];
static int  len = 0;

// Every start is the former boot, minus Serial.begin and the help text: the
// playlist, slot, styles and speed come back from NVS (defaults on first boot),
// scrolling resumes, and the button and line state start clean.
void start(ComtelPanel &panel) {
  hostPanel = &panel;
  scroller = ScrollText();
  scroller.attach(panel);
  btn1 = Btn{BTN_PIN_1};
  btn2 = Btn{BTN_PIN_2};
  speedIdx = 1;
  len = 0;
  pinMode(btn1.pin, INPUT_PULLUP);
  pinMode(btn2.pin, INPUT_PULLUP);
  loadSettings();                 // restore playlist/slot/styles/speed from NVS
  showSlot(msgIdx);               // primes + starts the scroll message
  status();
}

void tick(ComtelPanel &) {
  scroller.tick();
  switch (pollBtn(btn1)) {
    case 1: nextMessage(); saveSettings(); status(); break;
    case 2: scroller.setRunning(!scroller.running());          // pause / resume
            Serial.println(scroller.running() ? F("resumed") : F("paused")); break;
  }
  switch (pollBtn(btn2)) {
    case 1: speedIdx = (speedIdx + 1) % 3;
            scroller.setStepMs(speedPresets[speedIdx]); saveSettings(); status(); break;
    case 2: testCard(); break;
  }
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\r') continue;
    if (ch != '\n') { if (len < 129) line[len++] = ch; continue; }
    line[len] = 0; len = 0;
    handleLine(line);
  }
}

// ---- web settings (host) ----------------------------------------------------
// The host has already checked type and range; each set() does what the serial
// command does -- apply, save to NVS, print status -- and re-checks its input.
void getMessage(char *o, size_t n) { strlcpy(o, playlist[msgIdx], n); }
bool setMessage(const char *v)     { return setCurrentMessage(v); }

void getSlot(char *o, size_t n)    { snprintf(o, n, "%d", msgIdx); }
bool setSlot(const char *v) {
  int s = atoi(v);
  if (s < 0 || s >= NMSG) return false;
  showSlot(s); saveSettings(); status();
  return true;
}

void getSpeed(char *o, size_t n)   { snprintf(o, n, "%lu", (unsigned long)scroller.stepMs()); }
bool setSpeed(const char *v) {
  long ms = atol(v);
  if (ms < 5 || ms > 2000) return false;
  scroller.setStepMs(ms); saveSettings(); status();
  return true;
}

void getSize(char *o, size_t n)    { snprintf(o, n, "%d", scroller.size()); }
bool setSize(const char *v) {
  int sz = atoi(v);
  if (sz != 1 && sz != 2) return false;
  scroller.setSize(sz); saveSettings(); status();
  return true;
}

void getBold(char *o, size_t n)    { strlcpy(o, scroller.boldOn() ? "1" : "0", n); }
bool setBold(const char *v)        { scroller.setBold(atoi(v) != 0); saveSettings(); status(); return true; }

void getItalic(char *o, size_t n)  { strlcpy(o, scroller.italicOn() ? "1" : "0", n); }
bool setItalic(const char *v)      { scroller.setItalic(atoi(v) != 0); saveSettings(); status(); return true; }

const PluginSetting SETTINGS[] = {
  {"message", "Message",         SettingType::Text,   0, 127,  nullptr,   getMessage, setMessage},
  {"slot",    "Slot",            SettingType::Choice, 0, 0,    "0|1|2|3", getSlot,    setSlot},
  {"speed",   "Speed (ms/step)", SettingType::Int,    5, 2000, nullptr,   getSpeed,   setSpeed},
  {"size",    "Size",            SettingType::Choice, 0, 0,    "1|2",     getSize,    setSize},
  {"bold",    "Bold",            SettingType::Bool,   0, 0,    nullptr,   getBold,    setBold},
  {"italic",  "Italic",          SettingType::Bool,   0, 0,    nullptr,   getItalic,  setItalic},
};

}  // namespace

COMTEL_PLUGIN(scrolling_text, "Scrolling text with playlist, styles, buttons and NVS",
              start, tick, nullptr, SETTINGS);
