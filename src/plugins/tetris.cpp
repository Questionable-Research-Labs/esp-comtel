// @plugin: Tetris
// ============================================================================
//  tetris — full-screen 16×96 upright Tetris on the Comtel 96×16 LED sign.
//  Game logic ported from espTetris; rendering uses ComtelPanel (white cells).
//  Buttons: GPIO 32 (left), GPIO 33 (right), GPIO 2 (rotate / soft-drop).
// ============================================================================
#include <Arduino.h>
#include <esp_random.h>
#include "../driver/comtel_panel.h"
#include "../driver/button_pins.h"

static ComtelPanel panel;

// ---- board geometry (upright: 16 cols × 96 rows) --------------------------
namespace tetris {

static const uint8_t BOARD_ROWS = 96;
static const uint8_t BOARD_COLS = 16;
static const uint16_t FULL_ROW = 0xFFFF;
static const int8_t SPAWN_X = 6;

enum PieceType { I = 0, O, T, S, Z, J, L, PIECE_COUNT = 7 };

struct ActivePiece {
  PieceType type;
  uint8_t rot;
  int8_t x;
  int8_t y;
};

struct GameState {
  uint16_t board[BOARD_ROWS];
  ActivePiece active;
  uint32_t score;
  uint32_t gravityIntervalMs;
  uint32_t lastGravityMs;
  uint32_t topOutHoldUntilMs;
  bool toppedOut;
};

enum Action { NONE = 0, MOVE_LEFT, MOVE_RIGHT, ROTATE };

static const uint16_t PIECES[PIECE_COUNT][4] = {
    {0x0F00, 0x2222, 0x00F0, 0x4444},
    {0x6600, 0x6600, 0x6600, 0x6600},
    {0x0E40, 0x2620, 0x0270, 0x0464},
    {0x06C0, 0x4620, 0x0360, 0x0462},
    {0x0C60, 0x2640, 0x0630, 0x0264},
    {0x8E00, 0x3220, 0x0071, 0x044C},
    {0x1700, 0x0223, 0x00E8, 0xC440},
};

static const struct {
  uint32_t threshold;
  uint32_t intervalMs;
} GRAVITY_STEPS[9] = {
    {0, 700},     {500, 600},   {1500, 500},  {3000, 400},  {5000, 300},
    {8000, 220},  {12000, 160}, {18000, 120}, {25000, 90},
};

static const uint32_t SCORE_FOR_LINES[5] = {0, 100, 300, 500, 800};
static const uint32_t SOFT_DROP_INTERVAL_MS = 50;

static uint8_t bag[7];
static uint8_t bagIndex;

static void refillBag() {
  for (uint8_t i = 0; i < 7; i++) {
    bag[i] = i;
  }
  for (uint8_t i = 6; i > 0; i--) {
    const uint8_t j = random(i + 1);
    const uint8_t tmp = bag[i];
    bag[i] = bag[j];
    bag[j] = tmp;
  }
  bagIndex = 0;
}

static uint8_t nextPieceFromBag() {
  if (bagIndex >= 7) {
    refillBag();
  }
  return bag[bagIndex++];
}

static inline bool cellOf(PieceType type, uint8_t rot, uint8_t row, uint8_t col) {
  return (PIECES[type][rot] >> (15 - (row * 4 + col))) & 1;
}

bool pieceCell(PieceType type, uint8_t rot, uint8_t row, uint8_t col) {
  return cellOf(type, rot, row, col);
}

static bool collides(const GameState& state, PieceType type, uint8_t rot, int8_t x,
                     int8_t y) {
  for (uint8_t pr = 0; pr < 4; pr++) {
    for (uint8_t pc = 0; pc < 4; pc++) {
      if (!cellOf(type, rot, pr, pc)) {
        continue;
      }
      const int8_t bc = x + static_cast<int8_t>(pc);
      const int8_t br = y + static_cast<int8_t>(pr);
      if (bc < 0 || bc > 15) {
        return true;
      }
      if (br > 95) {
        return true;
      }
      if (br >= 0 && (state.board[br] & (1 << (15 - bc)))) {
        return true;
      }
    }
  }
  return false;
}

static bool tryMove(GameState& state, int8_t dx) {
  const int8_t nx = state.active.x + dx;
  if (collides(state, state.active.type, state.active.rot, nx, state.active.y)) {
    return false;
  }
  state.active.x = nx;
  return true;
}

static bool tryRotate(GameState& state) {
  const uint8_t nrot = (state.active.rot + 1) & 3;
  if (collides(state, state.active.type, nrot, state.active.x, state.active.y)) {
    return false;
  }
  state.active.rot = nrot;
  return true;
}

static bool tryGravityDrop(GameState& state) {
  const int8_t ny = state.active.y + 1;
  if (collides(state, state.active.type, state.active.rot, state.active.x, ny)) {
    return false;
  }
  state.active.y = ny;
  return true;
}

static uint8_t clearLines(GameState& state) {
  uint8_t cleared = 0;
  for (uint8_t row = 0; row < BOARD_ROWS; row++) {
    if (state.board[row] != FULL_ROW) {
      continue;
    }
    cleared++;
    for (int8_t r = row; r > 0; r--) {
      state.board[r] = state.board[r - 1];
    }
    state.board[0] = 0;
    row--;
  }
  return cleared;
}

static void updateGravityInterval(GameState& state) {
  state.gravityIntervalMs = GRAVITY_STEPS[0].intervalMs;
  for (uint8_t i = 0; i < 9; i++) {
    if (state.score >= GRAVITY_STEPS[i].threshold) {
      state.gravityIntervalMs = GRAVITY_STEPS[i].intervalMs;
    }
  }
}

static void spawnPiece(GameState& state) {
  state.active.type = static_cast<PieceType>(nextPieceFromBag());
  state.active.rot = 0;
  state.active.x = SPAWN_X;
  state.active.y = 0;
  if (collides(state, state.active.type, state.active.rot, state.active.x,
               state.active.y)) {
    state.toppedOut = true;
    state.topOutHoldUntilMs = millis() + 1000;
  }
}

static void lockPiece(GameState& state) {
  const PieceType type = state.active.type;
  for (uint8_t pr = 0; pr < 4; pr++) {
    for (uint8_t pc = 0; pc < 4; pc++) {
      if (!cellOf(type, state.active.rot, pr, pc)) {
        continue;
      }
      const int8_t bc = state.active.x + static_cast<int8_t>(pc);
      const int8_t br = state.active.y + static_cast<int8_t>(pr);
      if (bc < 0 || bc > 15 || br < 0 || br > 95) {
        continue;
      }
      state.board[br] |= static_cast<uint16_t>(1 << (15 - bc));
    }
  }

  const uint8_t lines = clearLines(state);
  if (lines > 0) {
    state.score += SCORE_FOR_LINES[lines];
    updateGravityInterval(state);
  }
  spawnPiece(state);
}

void reset(GameState& state) {
  for (uint8_t r = 0; r < BOARD_ROWS; r++) {
    state.board[r] = 0;
  }
  state.score = 0;
  state.gravityIntervalMs = 700;
  state.toppedOut = false;
  state.lastGravityMs = millis();
  refillBag();
  spawnPiece(state);
}

void init(GameState& state) {
  randomSeed(esp_random());
  refillBag();
  reset(state);
}

bool tick(GameState& state, uint32_t nowMs, Action action, bool softDropHeld) {
  if (state.toppedOut) {
    if (nowMs >= state.topOutHoldUntilMs) {
      reset(state);
      return true;
    }
    return false;
  }

  bool changed = false;

  if (action == MOVE_LEFT) {
    changed |= tryMove(state, -1);
  } else if (action == MOVE_RIGHT) {
    changed |= tryMove(state, 1);
  } else if (action == ROTATE) {
    changed |= tryRotate(state);
  }

  const uint32_t effectiveInterval =
      softDropHeld ? SOFT_DROP_INTERVAL_MS : state.gravityIntervalMs;
  if (nowMs - state.lastGravityMs >= effectiveInterval) {
    if (!tryGravityDrop(state)) {
      lockPiece(state);
    }
    state.lastGravityMs = nowMs;
    changed = true;
  }

  return changed;
}

}  // namespace tetris

// ---- buttons (GPIO 32 left, 33 right, 2 rotate/soft-drop) -------------------
static const uint8_t BTN_LEFT = BTN_PIN_1;
static const uint8_t BTN_RIGHT = BTN_PIN_2;
static const uint8_t BTN_ROTATE = BTN_PIN_EXTRA;
static const uint32_t ROTATE_LONG_PRESS_MS = 500;

struct ButtonState {
  uint8_t pin;
  bool lastReleased;
  uint32_t lastChangeMs;
  bool dasArmed;
  uint32_t pressStartMs;
  uint32_t lastRepeatMs;
  bool longPressArmed;
  bool pendingRotate;
};

static ButtonState BTN[3] = {
    {BTN_LEFT, true, 0, false, 0, 0, false, false},
    {BTN_RIGHT, true, 0, false, 0, 0, false, false},
    {BTN_ROTATE, true, 0, false, 0, 0, false, false},
};

static bool inputArmed = false;

static void configureButtonPins() {
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_ROTATE, INPUT_PULLUP);
}

static void initButtons(uint32_t nowMs) {
  for (uint8_t i = 0; i < 3; i++) {
    BTN[i].lastReleased = digitalRead(BTN[i].pin) == HIGH;
    BTN[i].lastChangeMs = nowMs;
    BTN[i].dasArmed = false;
    BTN[i].pressStartMs = 0;
    BTN[i].lastRepeatMs = 0;
    BTN[i].longPressArmed = false;
    BTN[i].pendingRotate = false;
  }
  inputArmed = false;
}

static bool allButtonsReleased() {
  return digitalRead(BTN_LEFT) == HIGH && digitalRead(BTN_RIGHT) == HIGH &&
         digitalRead(BTN_ROTATE) == HIGH;
}

static tetris::Action sampleButtons(uint32_t nowMs, bool& softDropHeld) {
  softDropHeld = false;

  if (!inputArmed) {
    if (!allButtonsReleased()) {
      return tetris::NONE;
    }
    inputArmed = true;
  }

  tetris::Action leftAction = tetris::NONE;
  tetris::Action rightAction = tetris::NONE;
  tetris::Action rotateAction = tetris::NONE;

  for (uint8_t i = 0; i < 3; i++) {
    ButtonState& btn = BTN[i];
    const bool released = digitalRead(btn.pin) == HIGH;

    if (released != btn.lastReleased) {
      if (nowMs - btn.lastChangeMs >= 20) {
        const bool wasReleased = btn.lastReleased;
        btn.lastChangeMs = nowMs;
        btn.lastReleased = released;

        if (!released && wasReleased) {
          btn.pressStartMs = nowMs;
          btn.dasArmed = false;
          btn.lastRepeatMs = nowMs;
          if (i == 0) {
            leftAction = tetris::MOVE_LEFT;
          } else if (i == 1) {
            rightAction = tetris::MOVE_RIGHT;
          } else {
            btn.pendingRotate = true;
            btn.longPressArmed = false;
          }
        }

        if (released) {
          btn.dasArmed = false;
          if (i == 2) {
            if (btn.pendingRotate) {
              rotateAction = tetris::ROTATE;
            }
            btn.pendingRotate = false;
            btn.longPressArmed = false;
          }
        }
      }
    }

    if (i == 2) {
      if (!btn.lastReleased && btn.pressStartMs != 0 && !btn.longPressArmed &&
          nowMs - btn.pressStartMs >= ROTATE_LONG_PRESS_MS) {
        btn.longPressArmed = true;
        btn.pendingRotate = false;
      }
      continue;
    }

    if (released) {
      continue;
    }

    if (btn.pressStartMs == 0) {
      continue;
    }

    if (!btn.dasArmed && nowMs - btn.pressStartMs >= 200) {
      btn.dasArmed = true;
      btn.lastRepeatMs = nowMs;
      if (i == 0) {
        leftAction = tetris::MOVE_LEFT;
      } else {
        rightAction = tetris::MOVE_RIGHT;
      }
    } else if (btn.dasArmed && nowMs - btn.lastRepeatMs >= 80) {
      btn.lastRepeatMs = nowMs;
      if (i == 0) {
        leftAction = tetris::MOVE_LEFT;
      } else {
        rightAction = tetris::MOVE_RIGHT;
      }
    }
  }

  softDropHeld = !BTN[2].lastReleased && BTN[2].longPressArmed;

  if (leftAction != tetris::NONE) {
    return leftAction;
  }
  if (rightAction != tetris::NONE) {
    return rightAction;
  }
  return rotateAction;
}

// ---- rendering (game gx,gy → canvas pixel gy,gx) ---------------------------
static void renderFrame(const tetris::GameState& state, bool invertFlash) {
  GFXcanvas8& c = panel.canvas;
  c.fillScreen(COMTEL_OFF);

  for (uint8_t gy = 0; gy < tetris::BOARD_ROWS; gy++) {
    for (uint8_t gx = 0; gx < tetris::BOARD_COLS; gx++) {
      bool on = (state.board[gy] & (1 << (15 - gx))) != 0;
      if (on) {
        c.drawPixel(gy, gx, COMTEL_WHITE);
      }
    }
  }

  if (!state.toppedOut) {
    for (uint8_t pr = 0; pr < 4; pr++) {
      for (uint8_t pc = 0; pc < 4; pc++) {
        if (!tetris::pieceCell(state.active.type, state.active.rot, pr, pc)) {
          continue;
        }
        const int8_t gx = state.active.x + static_cast<int8_t>(pc);
        const int8_t gy = state.active.y + static_cast<int8_t>(pr);
        if (gx >= 0 && gx < tetris::BOARD_COLS && gy >= 0 &&
            gy < tetris::BOARD_ROWS) {
          c.drawPixel(gy, gx, COMTEL_WHITE);
        }
      }
    }
  }

  if (invertFlash) {
    for (int16_t cx = 0; cx < ComtelPanel::W; cx++) {
      for (int16_t cy = 0; cy < ComtelPanel::H; cy++) {
        c.drawPixel(cx, cy, c.getPixel(cx, cy) ? COMTEL_OFF : COMTEL_WHITE);
      }
    }
  }

  panel.show();
}

// ---- app loop ---------------------------------------------------------------
static tetris::GameState gameState;
static uint32_t lastPrintedScore = UINT32_MAX;
static bool wasToppedOut = false;
static bool flashOn = false;
static uint32_t lastFlashMs = 0;

static void printScoreIfChanged() {
  if (gameState.score != lastPrintedScore) {
    Serial.printf("score: %lu\n", (unsigned long)gameState.score);
    lastPrintedScore = gameState.score;
  }
}

void setup() {
  Serial.begin(115200);
  panel.begin();
  configureButtonPins();
  initButtons(millis());
  tetris::init(gameState);
  lastPrintedScore = UINT32_MAX;
  renderFrame(gameState, false);
  printScoreIfChanged();
}

void loop() {
  panel.service();

  const uint32_t now = millis();
  bool softDropHeld = false;
  const tetris::Action action = sampleButtons(now, softDropHeld);
  const bool changed = tetris::tick(gameState, now, action, softDropHeld);

  if (gameState.toppedOut && !wasToppedOut) {
    Serial.printf("game over — score: %lu\n",
                  (unsigned long)gameState.score);
    lastPrintedScore = gameState.score;
  }
  wasToppedOut = gameState.toppedOut;

  bool needRedraw = changed;
  if (gameState.toppedOut) {
    if (now - lastFlashMs >= 150) {
      lastFlashMs = now;
      flashOn = !flashOn;
      needRedraw = true;
    }
  } else {
    flashOn = false;
  }

  if (needRedraw) {
    renderFrame(gameState, gameState.toppedOut && flashOn);
    printScoreIfChanged();
  }
}
