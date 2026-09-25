// @plugin: Bouncing 2x2 square (minimal reference plugin)
// ============================================================================
//  bouncer -- a 2x2 square tracing 45-deg diagonals, reflecting off all walls.
//  The smallest useful plugin: construct the panel, service() it, step the
//  square each loop. Depends only on the ComtelPanel driver; the animation
//  (folded in from the old bouncer.*) is file-local here.
// ============================================================================
#include <Arduino.h>
#include "../driver/comtel_panel.h"

static ComtelPanel panel;

static int  x = 0, y = 0, dx = 1, dy = 1;
static const uint32_t stepMs = 40;
static uint32_t last = 0;

void setup() {
  panel.begin();
  // random origin + 45-deg direction so each boot looks live. random(a,b)
  // yields a..b-1, so (0, W-1) keeps the 2x2 square fully on-screen.
  x = random(0, ComtelPanel::W - 1);
  y = random(0, ComtelPanel::H - 1);
  dx = random(0, 2) ? 1 : -1;
  dy = random(0, 2) ? 1 : -1;
  last = millis();
}

void loop() {
  panel.service();
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
