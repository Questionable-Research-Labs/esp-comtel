// @plugin: Fluid
// ============================================================================
//  fluid -- a landscape particle fluid on the Comtel 96x16 sign.
//
//  The panel stays horizontal: 96 columns, 16 rows, gravity toward the bottom
//  short edge. Each cell holds up to one particle of each primary (red, green,
//  blue). Different primaries share a cell and mix additively (yellow, cyan,
//  magenta, white). The same primary cannot stack in one cell.
//
//  Motion is a falling-sand liquid: each particle tries down, then a diagonal,
//  then a sideways slide, one cell per step. Blobs drop from the top at a
//  random X. Auto-spawn would fill the glass in about a minute; once about
//  half the cells are occupied the bottom row is discarded and everything
//  shifts down so the sim can keep running.
//
//  Buttons (internal pull-up, pin -> GND; see driver/button_pins.h):
//    GPIO 32 = drop red, GPIO 33 = drop green, GPIO 2 = drop blue,
//    GPIO 15 = drain the scene.
//  GPIO 2 and 15 are strapping/boot pins. Momentary buttons idle high, so
//  this is fine, but they must not be held low at reset.
//
//  Depends only on the shared driver headers.
// ============================================================================
#include <Arduino.h>
#include <esp_random.h>
#include <string.h>
#include "../driver/comtel_panel.h"
#include "../driver/button_pins.h"

static ComtelPanel panel;

static const int W = ComtelPanel::W;          // 96
static const int H = ComtelPanel::H;          // 16
static const int HALF_CELLS = (W * H) / 2;    // 768 -- auto-drain threshold

// 4x3 = 12 particles. At one block every SPAWN_MS that is ~27 particles/s,
// enough to fill 1536 cells in about a minute when nothing overlaps or drains.
static const int BLOCK_W = 4;
static const int BLOCK_H = 3;
static const uint32_t STEP_MS = 40;
static const uint32_t SPAWN_MS = 450;
static const uint32_t AUTO_DRAIN_MS = 3500;   // ~one packed row per 3.5 s
static const uint32_t DEBOUNCE_MS = 20;

static const uint8_t PRIMARIES[3] = {COMTEL_RED, COMTEL_GREEN, COMTEL_BLUE};

// Low 3 bits are the COMTEL_* palette: bit0 = R, bit1 = G, bit2 = B.
static uint8_t grid[H][W];
static uint8_t moved[H][W];   // channel bits that already moved this step

static bool sceneDraining = false;
static uint32_t lastStepMs = 0;
static uint32_t lastSpawnMs = 0;
static uint32_t lastDrainMs = 0;

// ---- occupancy ------------------------------------------------------------

static int occupiedCount() {
  int n = 0;
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      if (grid[y][x]) ++n;
    }
  }
  return n;
}

// Write `primary` into a BLOCK_W x BLOCK_H rectangle whose top-left is
// (x, 0). Cells that already hold that primary are left alone.
static void dropBlock(uint8_t primary, int x) {
  if (x < 0) x = 0;
  if (x > W - BLOCK_W) x = W - BLOCK_W;
  for (int dy = 0; dy < BLOCK_H; ++dy) {
    for (int dx = 0; dx < BLOCK_W; ++dx) {
      uint8_t& cell = grid[dy][x + dx];
      if (cell & primary) continue;
      cell = (uint8_t)(cell | primary);
    }
  }
}

static void spawnRandom() {
  dropBlock(PRIMARIES[random(0, 3)], random(0, W - BLOCK_W + 1));
}

// Discard the floor and shift every row down one. The top row is left empty.
static void shiftDown() {
  for (int y = H - 1; y > 0; --y) {
    for (int x = 0; x < W; ++x) grid[y][x] = grid[y - 1][x];
  }
  for (int x = 0; x < W; ++x) grid[0][x] = 0;
}

// ---- fluid step -----------------------------------------------------------

static bool tryMove(int x, int y, int nx, int ny, uint8_t bit) {
  if (nx < 0 || nx >= W || ny < 0 || ny >= H) return false;
  if (grid[ny][nx] & bit) return false;
  grid[y][x] = (uint8_t)(grid[y][x] & ~bit);
  grid[ny][nx] = (uint8_t)(grid[ny][nx] | bit);
  moved[ny][nx] = (uint8_t)(moved[ny][nx] | bit);
  return true;
}

// Bottom-up so a particle that falls does not move again this step. A
// sideways move into a not-yet-visited cell is skipped via `moved`.
static void stepFluid() {
  memset(moved, 0, sizeof moved);
  for (int y = H - 1; y >= 0; --y) {
    for (int x = 0; x < W; ++x) {
      const uint8_t here = grid[y][x];
      if (!here) continue;
      for (uint8_t bit = 1; bit <= 4; bit = (uint8_t)(bit << 1)) {
        if (!(here & bit)) continue;
        if (moved[y][x] & bit) continue;
        const int side = random(0, 2) ? 1 : -1;
        if (tryMove(x, y, x, y + 1, bit)) continue;
        if (tryMove(x, y, x + side, y + 1, bit)) continue;
        if (tryMove(x, y, x - side, y + 1, bit)) continue;
        if (tryMove(x, y, x + side, y, bit)) continue;
        tryMove(x, y, x - side, y, bit);
      }
    }
  }
}

static void renderFrame() {
  GFXcanvas8& c = panel.canvas;
  c.fillScreen(COMTEL_OFF);
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      if (grid[y][x]) c.drawPixel(x, y, grid[y][x] & 7);
    }
  }
  panel.show();
}

// ---- buttons --------------------------------------------------------------

enum BtnAction : uint8_t { BTN_DROP_RED, BTN_DROP_GREEN, BTN_DROP_BLUE, BTN_DRAIN };

struct Button {
  int pin;
  BtnAction action;
  bool lastReleased;
  uint32_t lastChangeMs;
};

static Button BTN[4] = {
    {BTN_PIN_1,       BTN_DROP_RED,   true, 0},  // GPIO 32
    {BTN_PIN_2,       BTN_DROP_GREEN, true, 0},  // GPIO 33
    {BTN_PIN_EXTRA,   BTN_DROP_BLUE,  true, 0},  // GPIO 2
    {BTN_PIN_EXTRA_2, BTN_DRAIN,      true, 0},  // GPIO 15
};

static bool inputArmed = false;

static void configureButtons(uint32_t nowMs) {
  for (uint8_t i = 0; i < 4; ++i) {
    pinMode(BTN[i].pin, INPUT_PULLUP);
    BTN[i].lastReleased = digitalRead(BTN[i].pin) == HIGH;
    BTN[i].lastChangeMs = nowMs;
  }
  inputArmed = false;
}

static bool allButtonsReleased() {
  for (uint8_t i = 0; i < 4; ++i) {
    if (digitalRead(BTN[i].pin) == LOW) return false;
  }
  return true;
}

static void pollButtons(uint32_t nowMs) {
  if (!inputArmed) {
    if (allButtonsReleased()) inputArmed = true;
    return;
  }

  for (uint8_t i = 0; i < 4; ++i) {
    Button& b = BTN[i];
    const bool released = digitalRead(b.pin) == HIGH;
    if (released == b.lastReleased) continue;
    if (nowMs - b.lastChangeMs < DEBOUNCE_MS) continue;

    const bool wasReleased = b.lastReleased;
    b.lastReleased = released;
    b.lastChangeMs = nowMs;
    if (released || !wasReleased) continue;  // release, or not a press edge

    if (b.action == BTN_DRAIN) {
      sceneDraining = true;
      continue;
    }
    if (sceneDraining) continue;  // colour drops wait until the scene is empty

    uint8_t colour = COMTEL_RED;
    if (b.action == BTN_DROP_GREEN) colour = COMTEL_GREEN;
    else if (b.action == BTN_DROP_BLUE) colour = COMTEL_BLUE;
    dropBlock(colour, random(0, W - BLOCK_W + 1));
  }
}

// ---- setup / loop ---------------------------------------------------------

void setup() {
  randomSeed(esp_random());
  panel.begin();
  configureButtons(millis());
  lastStepMs = millis();
  lastSpawnMs = millis();
  spawnRandom();
  renderFrame();
}

void loop() {
  panel.service();
  const uint32_t now = millis();
  pollButtons(now);
  if (now - lastStepMs < STEP_MS) return;
  lastStepMs = now;

  if (sceneDraining) {
    if (occupiedCount() == 0) {
      sceneDraining = false;
      lastSpawnMs = now;  // don't dump a backlog of missed spawns
    } else {
      shiftDown();
    }
  } else {
    stepFluid();
    if (now - lastSpawnMs >= SPAWN_MS) {
      lastSpawnMs = now;
      spawnRandom();
    }
    if (occupiedCount() >= HALF_CELLS && now - lastDrainMs >= AUTO_DRAIN_MS) {
      lastDrainMs = now;
      shiftDown();
    }
  }
  renderFrame();
}
