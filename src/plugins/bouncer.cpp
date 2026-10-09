// @plugin: Bouncing 2x2 square (minimal reference plugin)
// ============================================================================
//  bouncer -- a 2x2 square tracing 45-deg diagonals, reflecting off all walls.
//  The smallest useful plugin: re-randomise in start(), step the square each
//  tick(). Depends only on the ComtelPanel driver and the host interface; the
//  animation (folded in from the old bouncer.*) is file-local here.
// ============================================================================
#include <Arduino.h>
#include "../host/plugin.h"

namespace {

int  x = 0, y = 0, dx = 1, dy = 1;
const uint32_t stepMs = 40;
uint32_t last = 0;

void start(ComtelPanel &) {
  // random origin + 45-deg direction so each start looks live. random(a,b)
  // yields a..b-1, so (0, W-1) keeps the 2x2 square fully on-screen.
  x = random(0, ComtelPanel::W - 1);
  y = random(0, ComtelPanel::H - 1);
  dx = random(0, 2) ? 1 : -1;
  dy = random(0, 2) ? 1 : -1;
  last = millis();
}

void tick(ComtelPanel &panel) {
  if (millis() - last < stepMs) return;
  last = millis();

  // Predict-then-negate: flip the direction that would leave bounds, then step.
  // Handling axes independently makes corner bounces fall out (both negate).
  int nx = x + dx, ny = y + dy;
  if (nx < 0 || nx + 2 > ComtelPanel::W) dx = -dx;
  if (ny < 0 || ny + 2 > ComtelPanel::H) dy = -dy;
  x += dx;
  y += dy;

  GFXcanvas8 &c = panel.canvas;
  c.fillScreen(COMTEL_OFF);
  c.fillRect(x, y, 2, 2, COMTEL_WHITE);
  panel.show();
}

}  // namespace

COMTEL_PLUGIN(bouncer, "Bouncing 2x2 square (minimal reference plugin)",
              start, tick, nullptr);
