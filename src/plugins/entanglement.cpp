// @plugin: Entanglement
// ============================================================================
//  entanglement -- two-player game on the Comtel 96x16 sign.
//
//  START SCREEN: the sign boots to a static image (the explicit 16x96 array in
//  start_screen_image.h -- data only, edit it to change the artwork) and does
//  not play until someone asks it to. Any of the four buttons starts a fresh
//  game; that press is consumed by the start and does not also recolor or steal.
//  Thirty seconds with no button press abandons the game and puts the image
//  back, so a board left drifting in an empty room returns to the attract state
//  and the next player always begins from a clean panel.
//
//  Two 3x3 cubes (player 1 and player 2) drift across the panel, bouncing off
//  all four walls. Each cube moves along a discrete angle drawn from {22.5, 45,
//  67.5, 90} degrees measured FROM THE VERTICAL axis (so 90 = straight
//  lengthwise / horizontal, smaller = steeper) with a randomized speed. When the
//  two cubes overlap, both are re-randomized with a bias away from the collision,
//  and nudged apart so they don't stick.
//
//  COLOR IS THE MECHANIC: each cube starts a random color from {red, green,
//  blue} and steps one place through the cycle on its own COLOR button. Colors
//  decide what a player can pick up, and sharing one is what makes stealing
//  possible at all.
//
//  The two players cycle in OPPOSITE directions -- player 1 R -> G -> B -> R,
//  player 2 R -> B -> G -> R -- so two players mashing their color buttons at
//  the same rate keep meeting on a shared color instead of chasing each other
//  around the cycle forever out of phase.
//
//  ELECTRONS: fifteen single-pixel electrons exist as a fixed set (no respawn),
//  each a random one of the three colors, fixed for life. Each is in exactly one
//  of three states: FLOATING (stationary in space), ORBITING one player as a
//  slowly-rotating ring around that player's cube, or IN FLIGHT as a steal
//  crawling along the entanglement link. At game start all 15 float. A cube
//  AUTOMATICALLY collects any floating electron it runs over whose color matches
//  the cube's current color (mismatches are passed over). There is no hold cap --
//  the first player to hold eight at once (a strict majority) wins.
//
//  A cube-cube collision scatters every electron only when both cubes are the
//  same color; a different-color bump preserves both orbits.
//
//  ENTANGLEMENT: whenever the two cubes currently share a color they are
//  entangled -- a straight line in that shared color joins their center pixels,
//  and a rubber band pulls them together. The band accelerates each cube toward
//  the other, so one already fleeing keeps going for a moment, slows, turns
//  around, and comes back ever faster. Cycle either cube to a different color and
//  both the line and the pull stop instantly, leaving the cube with whatever
//  momentum the band gave it. The line is drawn under everything else, so it
//  never hides a cube or an electron.
//
//  STEALING IN FLIGHT: while entangled, the DIRECTION button stops steering and
//  instead launches a steal -- one of the opponent's held electrons, picked at
//  random regardless of its color, leaves their orbit and crawls along the link
//  toward the presser at a constant STEAL_SPEED px/s. Distance is therefore the
//  risk: a theft across the panel is a multi-second commitment, one at
//  near-contact lands almost at once, and the pull closing the gap keeps
//  shortening the trip. Its lifecycle:
//    launch  -- entangled press, opponent holds >= 1, no steal of yours in
//               flight already; otherwise the press does nothing.
//    capture -- it reaches the thief's cube while still entangled: joins their
//               orbit, slot freed for another steal.
//    forfeit -- either player breaks the shared color first: NOT captured, it
//               bursts off at a random angle from where it sat on the line and
//               settles as a loose floater anyone can collect.
//    wipe    -- a same-color cube touch mid-flight scatters everything including
//               it, so nothing is left in flight.
//  One steal per player at a time; both players may have one going at once, in
//  opposite directions along the same line. While entangled a player therefore
//  cannot change direction at all -- the color button is the only escape, and it
//  is also what forfeits their own steal.
//
//  Buttons (all internal-pull-up, no resistor -- see driver/button_pins.h),
//  one job each:
//    Player 1: GPIO 32 = cycle color, GPIO 2  = direction / steal
//    Player 2: GPIO 33 = cycle color, GPIO 15 = direction / steal
//  GPIO 2 and 15 are strapping/boot pins: momentary buttons idle high, so this
//  is fine, but they must not be held low at reset.
//
//  Depends only on the shared driver headers, plus its own two screen images
//  (start_screen_image.h and win_screen_image.h -- data only).
// ============================================================================
#include <Arduino.h>
#include <esp_random.h>
#include <math.h>
#include <string.h>
#include "../driver/comtel_panel.h"
#include "../driver/button_pins.h"
#include "start_screen_image.h"
#include "win_screen_image.h"

static ComtelPanel panel;

// ---- geometry & motion tuning ---------------------------------------------
static const int   W    = ComtelPanel::W;   // 96
static const int   H    = ComtelPanel::H;   // 16
static const int   CUBE = 3;                 // 3x3 cube
static const float SPEED_MIN = 8.0f;         // px/s -- tune on glass (task 8.3)
static const float SPEED_MAX = 24.0f;        // px/s
// Rubber-band pull applied to entangled cubes. PULL_ACCEL is the tuning knob: it
// sets how long a cube fleeing the other keeps going before the band turns it
// around (t = speed / PULL_ACCEL, so ~0.7 s from SPEED_MIN and ~1.8 s from
// SPEED_MAX). PULL_SPEED_MAX caps the speed the band can wind a cube up to, which
// also keeps per-frame travel near 1 px so nothing can tunnel through anything.
static const float PULL_ACCEL     = 12.0f;   // px/s^2 toward the other cube
static const float PULL_SPEED_MAX = 44.0f;   // px/s ceiling while pull-boosted

// How fast a stolen electron crawls the link. This is the range dial for the
// whole steal mechanic: travel time is distance / STEAL_SPEED, so lowering it
// makes long-range theft a hopeless gamble and raising it makes even a
// cross-panel snatch land before the victim can react. It wants to sit ABOVE the
// SPEED_MIN..SPEED_MAX cube speed range (or a victim coasting away simply
// outruns it) but BELOW PULL_SPEED_MAX, so the band's windup still buys a
// fleeing player time. 30 px/s is ~3 s across the full 96-px panel.
static const float STEAL_SPEED = 40.0f;      // px/s along the link

// Anti-repeat threshold for the direction button: a fresh draw is rejected when
// its unit velocity has dot(new, old) above this against the pre-press heading.
// 0.98 ~= cos(11.25 deg), half the 22.5-deg spacing of the angle set, so it
// rejects exactly the draw that reproduces the current heading (and a
// pull-warped heading a draw happens to land on top of) and nothing else.
// Raising it toward 1.0 lets near-identical headings through; lowering it starts
// forbidding genuinely different angles and shrinks the choices to a wedge.
static const float DIR_REPEAT_DOT  = 0.98f;
static const int   DIR_REPEAT_TRIES = 8;     // bounded, so a press is never a no-op

// Angle set measured FROM VERTICAL: vx ~ sin(theta), vy ~ cos(theta).
// idx 3 = 90 deg -> cos 0 -> purely horizontal (lengthwise).
static const float SINV[4] = {0.38268f, 0.70711f, 0.92388f, 1.0f};
static const float COSV[4] = {0.92388f, 0.70711f, 0.38268f, 0.0f};

// Color cycle: every cube and electron color is one of these three pure
// channels. Cubes store an index into it so the button can step R -> G -> B -> R.
static const uint8_t CYCLE[3] = {COMTEL_RED, COMTEL_GREEN, COMTEL_BLUE};
static const int     N_COLORS = 3;

struct Cube {
  float   px, py;    // top-left origin (sub-pixel)
  float   vx, vy;    // velocity, px/s
  uint8_t colorIdx;  // index into CYCLE -- the cube's current color
};

// Named for the players, not their colors -- both start a random color and can
// be cycled to any of the three by their own button.
static Cube red;    // player 1 (index 0)
static Cube blue;   // player 2 (index 1)

static inline Cube& cubeFor(uint8_t player) { return player == 0 ? red : blue; }

static inline uint8_t cubeColor(const Cube& c) { return CYCLE[c.colorIdx]; }

// Integer cell origin of a cube (floor of its sub-pixel top-left).
static inline int cellX(const Cube& c) { return (int)floorf(c.px); }
static inline int cellY(const Cube& c) { return (int)floorf(c.py); }

// Integer center cell of a cube -- the exact middle pixel for an odd CUBE.
static inline int centerX(const Cube& c) { return cellX(c) + CUBE / 2; }
static inline int centerY(const Cube& c) { return cellY(c) + CUBE / 2; }

// Entangled = the two cubes currently share a color. This one condition drives
// both the link line and the mutual pull, so the line is exactly the tell for
// "these two are being drawn together".
static inline bool entangled() { return cubeColor(red) == cubeColor(blue); }

// ---- electrons -------------------------------------------------------------
// Fixed set of N_ELECTRONS (15) single-pixel electrons, each with its own fixed
// color.
//
// There is no hold cap, and the win condition is holding WIN_ELECTRONS (8) at
// once -- a strict majority of an odd total, which is what makes it a place only
// one player can ever stand. A cap below 8 would make the game unwinnable, and
// any cap at or above it is moot, since the win test fires on the same frame. So
// the rule is simply "hold as many as you like; holding 8 wins".
//
// `owner` is the state discriminator:
//   -1 = FLOATING at (x, y);  0 = orbiting red;  1 = orbiting blue;
//   OWNER_FLIGHT (-2) = IN FLIGHT as a steal along the link.
// Floating electrons carry a velocity (normally zero -- they sit still) so the
// collision "release" can fling them outward; friction brings them back to rest.
// When orbiting, (x, y, vx, vy) are ignored -- the drawn position is derived
// from the owner's cube border plus the global orbit phase. In flight they are
// ignored too: the position comes from stealDist along the live link.
//
// Flight is a NEGATIVE sentinel on purpose -- it makes every existing pass skip
// an in-flight electron for free. integrateElectrons and the floating draw test
// `owner == -1`, autoCollect bails on `owner != -1`, and orbitCount counts
// `owner == player`, so an electron in flight moves under its own rules and
// counts for nobody without a line of new bookkeeping in any of them.
static const int   N_ELECTRONS  = 15;
static const int   WIN_ELECTRONS  = 8;
static const int8_t OWNER_FLIGHT = -2;   // in flight as a steal (see stealIdx)

// Orbit path: the electrons trace the border one pixel outside the 3x3 cube,
// i.e. the perimeter of the surrounding 5x5 box (16 cells, symmetric on the
// cube so the orbit has no center offset).
static const int   ORBIT_BOX   = CUBE + 2;              // 5
static const int   ORBIT_PERIM = 4 * (ORBIT_BOX - 1);   // 16 cells
static const float ORBIT_STEP  = 0.20f;  // perimeter-cells per frame (~2s/rev)

// Collision release: high initial speed, heavy per-frame friction, snap to rest.
// Friction is randomized per electron within [FRICTION_MIN, FRICTION_MAX] at
// release time so they decelerate at different rates and don't all stop at once.
static const float RELEASE_SPEED = 140.0f;  // px/s outward burst
static const float FRICTION_MIN  = 0.84f;   // velocity retained per frame (fast stop)
static const float FRICTION_MAX  = 0.93f;   // (slow stop)
static const float REST_EPS      = 4.0f;    // px/s below which we stop

struct Electron {
  float   x, y;      // position (valid when floating; ignored when orbiting)
  float   vx, vy;    // velocity px/s (0 at rest; nonzero only just after release)
  float   friction;  // per-frame velocity retention while released (set on release)
  int8_t  owner;     // -1 floating, 0 red, 1 blue, OWNER_FLIGHT in flight
  uint8_t color;     // fixed for life -- collect/steal/release never change it
};

static Electron electrons[N_ELECTRONS];
static float    orbitPhase = 0.0f;  // perimeter position, in cells [0, ORBIT_PERIM)

// One steal slot per player: the electron they currently have in flight and how
// far it has crawled along the link from the victim's centre. stealIdx[p] >= 0 is
// both "player p has a steal in flight" (the one-per-player gate) and the
// identity of the electron.
//
// INVARIANT: stealIdx[p] >= 0 exactly when some electron has owner ==
// OWNER_FLIGHT for p, and every exit from flight -- capture, forfeit, and the
// same-color wipe -- must clear the slot AND rewrite that electron's owner. Miss
// either half and an electron is stranded invisible or double-owned, so all
// three paths funnel through releaseInFlight/updateSteals rather than editing
// the pair by hand.
static int8_t stealIdx[2]  = {-1, -1};  // electron index in flight, -1 = none
static float  stealDist[2] = {0.0f, 0.0f};  // px travelled from victim's centre

static int orbitCount(uint8_t player) {
  int n = 0;
  for (int i = 0; i < N_ELECTRONS; i++)
    if (electrons[i].owner == (int8_t)player) n++;
  return n;
}

// Map a perimeter parameter t (in cells) to a cell on the border of the 5x5 box
// whose top-left is (x0, y0). Walks clockwise: top, right, bottom, left edges.
static void borderCell(int x0, int y0, float t, int& ox, int& oy) {
  int k = (int)floorf(t) % ORBIT_PERIM;
  if (k < 0) k += ORBIT_PERIM;
  const int side = ORBIT_BOX - 1;  // 4
  if (k < side)            { ox = x0 + k;            oy = y0; }
  else if (k < 2 * side)   { ox = x0 + side;         oy = y0 + (k - side); }
  else if (k < 3 * side)   { ox = x0 + side - (k - 2 * side); oy = y0 + side; }
  else                     { ox = x0;                oy = y0 + side - (k - 3 * side); }
}

// On-screen cell of orbiting electron slot i of n around a cube's border.
static void orbitPos(const Cube& c, int i, int n, int& ox, int& oy) {
  const float t = orbitPhase + (float)i * ((float)ORBIT_PERIM / (float)n);
  borderCell(cellX(c) - 1, cellY(c) - 1, t, ox, oy);
}

// Length of the link in pixels, measured between the same centre cells the link
// line is drawn between, so a steal's travel distance is exactly the distance the
// player can see. Re-evaluated every frame: the pull shortens the trip as it
// draws the cubes together, and lengthens it while a fleeing cube still coasts.
static float linkLength() {
  const float dx = (float)(centerX(blue) - centerX(red));
  const float dy = (float)(centerY(blue) - centerY(red));
  return sqrtf(dx * dx + dy * dy);
}

// Current cell of the electron `thief` has in flight: the victim's centre plus
// stealDist[thief] pixels along the unit vector toward the thief's centre,
// clamped to the thief's end. Derived from the live centres each frame rather
// than integrated as a free position, so the pixel is always exactly ON the drawn
// line however both cubes have moved, and an oversized dt can never carry it off
// the line or past its target.
static void inFlightCell(uint8_t thief, int& x, int& y) {
  const Cube& vc = cubeFor(thief ^ 1);
  const Cube& tc = cubeFor(thief);
  const float vx = (float)centerX(vc), vy = (float)centerY(vc);
  const float dx = (float)centerX(tc) - vx, dy = (float)centerY(tc) - vy;
  const float d  = sqrtf(dx * dx + dy * dy);
  if (d < 0.001f) {                 // coincident centres: no direction to ride
    x = (int)vx; y = (int)vy;
    return;
  }
  const float t = fminf(stealDist[thief], d);
  x = (int)lroundf(vx + dx / d * t);
  y = (int)lroundf(vy + dy / d * t);
}

// ---- RNG helpers -----------------------------------------------------------
static inline float randSign() { return random(0, 2) ? 1.0f : -1.0f; }

static inline float randSpeed() {
  return SPEED_MIN + (float)random(0, 1001) / 1000.0f * (SPEED_MAX - SPEED_MIN);
}

// Pick a fresh discrete angle, quadrant, and speed for a cube.
static void randomizeMotion(Cube& cube) {
  const int   idx   = random(0, 4);
  const float speed = randSpeed();
  cube.vx = randSign() * speed * SINV[idx];
  cube.vy = randSign() * speed * COSV[idx];  // 0 at 90 deg regardless of sign
}

// Direction-button action: re-randomize a cube's motion, but guarantee the press
// visibly changes its course. Compares UNIT VELOCITIES rather than the drawn
// (angleIdx, signX, signY) tuple, because a wall bounce mirrors a component and
// the entangled pull rewrites the velocity outright -- so a remembered tuple
// stops describing where the cube is actually heading, which is the only thing a
// player can see. The retry bound keeps the worst case O(1) and accepting the
// last draw means a press is never silently a no-op.
static void randomizeDirection(Cube& cube) {
  const float s2 = cube.vx * cube.vx + cube.vy * cube.vy;
  if (s2 < 0.001f) {           // standing still: no heading to differ from
    randomizeMotion(cube);
    return;
  }
  const float s = sqrtf(s2);
  const float oldUx = cube.vx / s, oldUy = cube.vy / s;

  for (int tries = 0; tries < DIR_REPEAT_TRIES; tries++) {
    randomizeMotion(cube);
    const float n2 = cube.vx * cube.vx + cube.vy * cube.vy;
    if (n2 < 0.001f) continue;
    const float n = sqrtf(n2);
    if ((cube.vx / n) * oldUx + (cube.vy / n) * oldUy <= DIR_REPEAT_DOT) return;
  }
  // Bound exhausted -- keep the last draw. Speed almost certainly differs even
  // when the heading repeats, and a dead button would be worse than a rare
  // press that only changes pace.
}

// Color-button action: step this player's cube one place through CYCLE. The two
// players step in OPPOSITE directions -- player 0 forward, player 1 backward --
// and that asymmetry is deliberate, not a bug to be tidied up: with both cycling
// the same way, two players pressing at a similar rate stay locked at a fixed
// offset and can go a long time without ever sharing a color. Counter-rotating
// cycles make them sweep past each other, so equal-rate pressing keeps producing
// entanglement.
static void cycleColor(uint8_t player) {
  Cube& cube = cubeFor(player);
  const int step = (player == 0) ? 1 : (N_COLORS - 1);  // +1 forward, -1 backward
  cube.colorIdx = (uint8_t)((cube.colorIdx + step) % N_COLORS);
}

// ---- motion & walls --------------------------------------------------------
static void integrateAndBounce(Cube& c, float dt) {
  c.px += c.vx * dt;
  c.py += c.vy * dt;

  // Reflect + clamp in one step: correcting to the edge guarantees the whole
  // 3x3 cube stays on-screen even after a large dt (a long show() stall).
  if (c.px < 0)             { c.px = 0;        c.vx = -c.vx; }
  else if (c.px > W - CUBE) { c.px = W - CUBE; c.vx = -c.vx; }
  if (c.py < 0)             { c.py = 0;        c.vy = -c.vy; }
  else if (c.py > H - CUBE) { c.py = H - CUBE; c.vy = -c.vy; }
}

// Advance released (moving) floating electrons: integrate, bounce off the screen
// sides, bleed speed with heavy friction, and snap to rest on the grid once slow.
static void integrateElectrons(float dt) {
  for (int i = 0; i < N_ELECTRONS; i++) {
    Electron& e = electrons[i];
    if (e.owner != -1) continue;
    if (e.vx == 0.0f && e.vy == 0.0f) continue;  // already at rest

    e.x += e.vx * dt;
    e.y += e.vy * dt;
    if (e.x < 0)          { e.x = 0;         e.vx = -e.vx; }
    else if (e.x > W - 1) { e.x = W - 1;     e.vx = -e.vx; }
    if (e.y < 0)          { e.y = 0;         e.vy = -e.vy; }
    else if (e.y > H - 1) { e.y = H - 1;     e.vy = -e.vy; }

    e.vx *= e.friction;
    e.vy *= e.friction;
    if (e.vx * e.vx + e.vy * e.vy < REST_EPS * REST_EPS) {
      e.vx = 0.0f; e.vy = 0.0f;
      e.x = roundf(e.x); e.y = roundf(e.y);
    }
  }
}

// ---- entangled attraction ---------------------------------------------------
// Hold a cube's speed at or below the pull ceiling, preserving its direction.
static void clampPullSpeed(Cube& c) {
  const float s2 = c.vx * c.vx + c.vy * c.vy;
  if (s2 <= PULL_SPEED_MAX * PULL_SPEED_MAX) return;
  const float s = sqrtf(s2);
  c.vx = c.vx / s * PULL_SPEED_MAX;
  c.vy = c.vy / s * PULL_SPEED_MAX;
}

// While the cubes share a color, accelerate each toward the other -- a rubber
// band between them. This acts on VELOCITY, so momentum is preserved: a cube
// already heading away keeps going that way, decelerating, for speed/PULL_ACCEL
// seconds before the band turns it around and it comes back faster and faster.
// That windup is the whole point, so unlike a positional drift it deliberately
// takes each cube off the discrete angle set and past its randomized speed;
// PULL_SPEED_MAX bounds how far, and the next re-randomize or collision puts the
// cube back on the angle set.
static void applyAttraction(float dt) {
  if (!entangled()) return;

  // Same cube size, so the origin delta is also the center delta.
  const float dx = blue.px - red.px;
  const float dy = blue.py - red.py;
  const float d  = sqrtf(dx * dx + dy * dy);
  if (d < 0.001f) return;  // already coincident -- no direction to pull along

  const float ux = dx / d, uy = dy / d;
  const float dv = PULL_ACCEL * dt;

  red.vx  += ux * dv;  red.vy  += uy * dv;   // red pulled toward blue
  blue.vx -= ux * dv;  blue.vy -= uy * dv;   // and blue toward red
  clampPullSpeed(red);
  clampPullSpeed(blue);
}

// ---- cube-cube collision ---------------------------------------------------
static bool cubesOverlap() {
  return red.px < blue.px + CUBE && blue.px < red.px + CUBE &&
         red.py < blue.py + CUBE && blue.py < red.py + CUBE;
}

// Re-randomize a cube, then flip its whole velocity if it points toward the
// other cube, so the new (still random-angle) heading lands in the hemisphere
// away from the collision vector (ax, ay) = this-center minus other-center.
static void randomizeAway(Cube& c, float ax, float ay) {
  randomizeMotion(c);
  if (c.vx * ax + c.vy * ay < 0.0f) {
    c.vx = -c.vx;
    c.vy = -c.vy;
  }
}

// Detach every electron and fling it outward from (cx, cy), the collision point.
// Each starts from wherever it is visible right now (floating spot, orbit position
// or its cell on the link) so the burst reads as coming off the cubes; friction
// later settles them back to a resting float. Electrons in flight are included:
// the wipe cancels a steal that was about to land, and both steal slots are
// cleared below so nothing is left stranded in flight afterwards.
static void releaseAllElectrons(float cx, float cy) {
  const int nRed = orbitCount(0), nBlue = orbitCount(1);
  int iRed = 0, iBlue = 0;
  for (int i = 0; i < N_ELECTRONS; i++) {
    Electron& e = electrons[i];
    float ex, ey;
    if (e.owner == -1) {
      ex = e.x; ey = e.y;
    } else if (e.owner == OWNER_FLIGHT) {
      int ox = 0, oy = 0;
      // Whichever player has it in flight; the slots are cleared right after.
      const uint8_t thief = (stealIdx[0] == (int8_t)i) ? 0 : 1;
      inFlightCell(thief, ox, oy);
      ex = (float)ox; ey = (float)oy;
    } else {
      int ox, oy;
      if (e.owner == 0) orbitPos(red,  iRed++,  nRed,  ox, oy);
      else              orbitPos(blue, iBlue++, nBlue, ox, oy);
      ex = (float)ox; ey = (float)oy;
    }

    float dx = ex - cx, dy = ey - cy;
    float d  = sqrtf(dx * dx + dy * dy);
    if (d < 0.001f) {                 // sitting on the collision point: fan out by index
      dx = (i & 1) ? 1.0f : -1.0f;
      dy = (i & 2) ? 1.0f : -1.0f;
      d  = sqrtf(dx * dx + dy * dy);
    }

    e.owner = -1;
    e.x = ex; e.y = ey;
    e.vx = RELEASE_SPEED * dx / d;
    e.vy = RELEASE_SPEED * dy / d;
    e.friction = FRICTION_MIN +
                 (float)random(0, 1001) / 1000.0f * (FRICTION_MAX - FRICTION_MIN);
  }

  // Every electron is floating now, so no steal survives the wipe: clear both
  // slots to keep the stealIdx/owner invariant intact.
  stealIdx[0] = stealIdx[1] = -1;
  stealDist[0] = stealDist[1] = 0.0f;
}

static void resolveCollision() {
  const float rcx = red.px + CUBE / 2.0f, rcy = red.py + CUBE / 2.0f;
  const float bcx = blue.px + CUBE / 2.0f, bcy = blue.py + CUBE / 2.0f;

  // Only a SAME-color collision scatters the electrons. A different-color bump
  // leaves both orbits intact so a player can linger in stealing range; the
  // re-randomize and separation below still run either way, so cubes never stick.
  if (cubeColor(red) == cubeColor(blue))
    releaseAllElectrons((rcx + bcx) / 2.0f, (rcy + bcy) / 2.0f);

  float ax = rcx - bcx;  // points from blue toward red
  float ay = rcy - bcy;

  if (ax == 0.0f && ay == 0.0f) {
    // Coincident centers: fall back to opposing horizontal directions.
    ax = 1.0f;
    ay = 0.0f;
  }

  randomizeAway(red, ax, ay);    // red heads along +a
  randomizeAway(blue, -ax, -ay); // blue heads along -a

  // Separate along the axis of least penetration so the overlap clears and the
  // collision does not retrigger next frame.
  const float ox = CUBE - fabsf(ax);  // x penetration (|ax| < CUBE here)
  const float oy = CUBE - fabsf(ay);  // y penetration
  if (ox <= oy) {
    const float push = (ax >= 0.0f ? 1.0f : -1.0f) * (ox / 2.0f + 0.01f);
    red.px += push;
    blue.px -= push;
  } else {
    const float push = (ay >= 0.0f ? 1.0f : -1.0f) * (oy / 2.0f + 0.01f);
    red.py += push;
    blue.py -= push;
  }

  // Keep both fully on-screen after the nudge.
  red.px = constrain(red.px, 0.0f, (float)(W - CUBE));
  red.py = constrain(red.py, 0.0f, (float)(H - CUBE));
  blue.px = constrain(blue.px, 0.0f, (float)(W - CUBE));
  blue.py = constrain(blue.py, 0.0f, (float)(H - CUBE));
}

// ---- proximity & auto-collect ----------------------------------------------
// "Within 1 pixel" = Chebyshev gap <= 1 between two axis-aligned integer boxes
// (box a = w x h cells with top-left (ax, ay); box b likewise). Returns true
// when the boxes touch or leave at most a 1-pixel gap on each axis. Used to keep
// the startup electron placement clear of both cubes; adjacency no longer gates
// stealing (sharing a color does).
static bool within1px(int ax, int ay, int aw, int ah,
                      int bx, int by, int bw, int bh) {
  const int gapX = max(0, max(ax - (bx + bw - 1), bx - (ax + aw - 1)));
  const int gapY = max(0, max(ay - (by + bh - 1), by - (ay + ah - 1)));
  return gapX <= 1 && gapY <= 1;
}

// Automatic collection: any floating electron the cube has run over -- its pixel
// inside the cube's 3x3 area -- joins that cube's orbit, but only if its color
// matches the cube's CURRENT color. Mismatched colors are passed over. Runs every
// frame; no button involved.
//
// Nothing here counts what the player already holds: there is no cap, so a
// collection is never skipped for want of room. The ninth pickup is exactly the
// one that must NOT be refused -- it is the win.
static void autoCollect(uint8_t player) {
  const Cube&   cube = cubeFor(player);
  const uint8_t want = cubeColor(cube);
  const int     cx = cellX(cube), cy = cellY(cube);

  for (int i = 0; i < N_ELECTRONS; i++) {
    Electron& e = electrons[i];
    if (e.owner != -1) continue;
    if (e.color != want) continue;
    const int ex = (int)lroundf(e.x), ey = (int)lroundf(e.y);
    if (ex < cx || ex >= cx + CUBE || ey < cy || ey >= cy + CUBE) continue;
    e.owner = (int8_t)player;
  }
}

// ---- steal in flight -------------------------------------------------------
// Pick one of `victim`'s orbiting electrons uniformly at random and return its
// index, or -1 when they hold none. No color filter: entanglement already
// requires the two cubes to share a color, so a color-matched steal would usually
// find nothing and the press would silently die -- exactly the dead-button
// failure this mechanic replaced.
static int stealTargetIdx(uint8_t victim) {
  const int n = orbitCount(victim);
  if (n <= 0) return -1;
  int k = random(0, n);
  for (int i = 0; i < N_ELECTRONS; i++) {
    if (electrons[i].owner != (int8_t)victim) continue;
    if (k-- == 0) return i;
  }
  return -1;  // unreachable: orbitCount counted them
}

// Direction-button action while entangled: launch a steal. A no-op unless the
// cubes share a color, this player has no steal of their own already in flight,
// and the opponent actually holds something. Never touches either cube's
// velocity or color.
//
// No hold-cap check is needed here, and none is needed in autoCollect either,
// because there is no cap to check. A capture that brings the thief to
// WIN_ELECTRONS does not need refusing -- it wins them the game, and
// the win test at the end of the frame picks it up.
static void tryStealFlight(uint8_t player) {
  if (!entangled()) return;
  if (stealIdx[player] >= 0) return;         // one steal per player at a time

  const int idx = stealTargetIdx(player ^ 1);
  if (idx < 0) return;                       // opponent holds nothing

  electrons[idx].owner = OWNER_FLIGHT;       // leaves the victim's orbit at once
  stealIdx[player]  = (int8_t)idx;
  stealDist[player] = 0.0f;                  // starts at the victim's centre
}

// Forfeit `player`'s steal: the electron is NOT captured. It becomes an ordinary
// floater at wherever it sat on the line, bursting off along a uniformly random
// heading with a randomized friction, and rides the existing integrateElectrons
// bounce-and-settle path -- same visual vocabulary as a collision fragment, and
// collectable by either cube once it comes to rest. Clears the slot.
//
// Tuning settled on glass: the burst keeps the collision path's full
// RELEASE_SPEED rather than a slower one of its own, so a forfeit reads as the
// same kind of event as a scatter and the player learns one vocabulary, not two.
static void releaseInFlight(uint8_t player) {
  const int8_t idx = stealIdx[player];
  if (idx < 0) return;

  int ox = 0, oy = 0;
  inFlightCell(player, ox, oy);

  Electron& e = electrons[idx];
  e.owner = -1;
  e.x = (float)ox; e.y = (float)oy;
  const float a = (float)random(0, 6283) / 1000.0f;  // uniform heading, radians
  e.vx = RELEASE_SPEED * cosf(a);
  e.vy = RELEASE_SPEED * sinf(a);
  e.friction = FRICTION_MIN +
               (float)random(0, 1001) / 1000.0f * (FRICTION_MAX - FRICTION_MIN);

  stealIdx[player]  = -1;
  stealDist[player] = 0.0f;
}

// One frame of steal progress, in this order for both players:
//   1. FORFEIT first -- the link is gone, so nothing can land. Testing before
//      advancing means a colour press can never be overtaken by a capture in the
//      same frame: the escape always wins the tie.
//   2. ADVANCE at a constant px/s, which is what makes range the risk.
//   3. CAPTURE once the distance covered reaches THIS frame's centre-to-centre
//      distance, so the closing pull brings the target to the electron.
static void updateSteals(float dt) {
  if (!entangled()) {
    releaseInFlight(0);
    releaseInFlight(1);
    return;
  }

  const float d = linkLength();
  for (uint8_t p = 0; p < 2; p++) {
    if (stealIdx[p] < 0) continue;
    stealDist[p] += STEAL_SPEED * dt;
    if (stealDist[p] < d) continue;

    electrons[stealIdx[p]].owner = (int8_t)p;   // captured -- joins the orbit
    stealIdx[p]  = -1;
    stealDist[p] = 0.0f;
  }
}

// ---- start / play / win modes ----------------------------------------------
// Three modes, and only four transitions between them:
//   START -> PLAY   a button press begins a game
//   PLAY  -> START  the idle timeout expires and abandons it
//   PLAY  -> WIN    a player's orbit holds WIN_ELECTRONS (8) electrons
//   WIN   -> START  a press after the lockout, or the unattended timeout
// There is deliberately NO WIN -> PLAY edge: a new game is always begun from
// START, so "a fresh game" has exactly one meaning and one code path.
//
// Neither START nor WIN does ANY per-frame work. enterStart()/enterWin() push
// their image once and panel.service() alone holds a latched frame lit (show()
// writes the panel's own registers; service() only walks the eight scan
// addresses), so re-drawing a picture that cannot change would burn ~12 ms of
// every 25 ms cycle for nothing.
enum Mode : uint8_t { MODE_START, MODE_PLAY, MODE_WIN };
static Mode mode = MODE_START;

// Idle timeout: 30 s with no button press abandons the game back to the start
// screen. Only debounced press edges touch lastInputMs -- cube motion,
// collisions and collections deliberately do not, so a board left drifting in an
// empty room still times out.
static const uint32_t IDLE_TIMEOUT_MS = 30000;
static uint32_t lastInputMs = 0;

// Win screen timing, both measured from winEnteredMs.
//
// The lockout is not politeness, it is necessity: the ninth electron usually
// arrives via a steal the winner has just pressed for, so their button is very
// often STILL DOWN when the win screen appears. Without a lockout that button's
// release-and-press would dismiss the screen before anyone read it. Two seconds
// covers the reaction time of a finger already resting on a button.
//
// The unattended return is written as LOCKOUT + HOLD rather than as a flat 7000
// so "ignores input for 2 s, then 5 s more" reads straight off the code.
static const uint32_t WIN_LOCKOUT_MS = 2000;   // all input ignored and consumed
static const uint32_t WIN_HOLD_MS    = 5000;   // then this long with no press
static uint32_t winEnteredMs = 0;

// Both defined below render(): startGame() calls it, and the two screen-entering
// functions sit beside it. pollButtons() needs enterStart() to dismiss the win
// screen, hence the forward declaration.
static void startGame();
static void enterStart();

// ---- buttons (debounced edge detect, tetris pattern) ----------------------
// Two buttons per player, ONE JOB EACH: the color button only recolors, the
// direction button only changes course (or, while entangled, only launches a
// steal). Neither ever does the other's job, and neither touches the opponent's
// cube.
enum BtnAction : uint8_t {
  BTN_COLOR,      // step this player's color one place through the cycle
  BTN_DIRECTION,  // free: re-randomize velocity; entangled: launch a steal
};

struct Button {
  uint8_t   pin;
  uint8_t   player;   // 0 = red, 1 = blue
  BtnAction action;
  bool      lastReleased;
  uint32_t  lastChangeMs;
};

static Button BTN[4] = {
    {BTN_PIN_1,       0, BTN_COLOR,     true, 0},  // GPIO 32 -- P1 color
    {BTN_PIN_EXTRA,   0, BTN_DIRECTION, true, 0},  // GPIO 2  -- P1 direction/steal
    {BTN_PIN_2,       1, BTN_COLOR,     true, 0},  // GPIO 33 -- P2 color
    {BTN_PIN_EXTRA_2, 1, BTN_DIRECTION, true, 0},  // GPIO 15 -- P2 direction/steal
};

static bool inputArmed = false;

static void configureButtons(uint32_t nowMs) {
  for (uint8_t i = 0; i < 4; i++) {
    pinMode(BTN[i].pin, INPUT_PULLUP);
    BTN[i].lastReleased = digitalRead(BTN[i].pin) == HIGH;
    BTN[i].lastChangeMs = nowMs;
  }
  inputArmed = false;
}

static bool allButtonsReleased() {
  for (uint8_t i = 0; i < 4; i++) {
    if (digitalRead(BTN[i].pin) == LOW) return false;
  }
  return true;
}

static void pollButtons(uint32_t nowMs) {
  // Ignore input until every button has been seen released once, so a button
  // held (or a boot glitch) at startup can't fire a spurious randomize. This is
  // also what makes "a button held down at boot cannot start a game" true: a
  // held button is never a press edge, so it never reaches the START gate below.
  if (!inputArmed) {
    if (allButtonsReleased()) inputArmed = true;
    return;
  }

  for (uint8_t i = 0; i < 4; i++) {
    Button& b = BTN[i];
    const bool released = digitalRead(b.pin) == HIGH;
    if (released == b.lastReleased) continue;
    if (nowMs - b.lastChangeMs < 20) continue;  // debounce

    const bool wasReleased = b.lastReleased;
    b.lastReleased = released;
    b.lastChangeMs = nowMs;
    if (!released && wasReleased) {         // press edge
      // Any press is activity, in any mode and whether or not it has an in-game
      // effect: a steal that finds nothing to take still says a human is standing
      // here, which is the only thing the idle timeout is measuring. Left
      // unconditional rather than moved below the gates -- in START and WIN the
      // write is simply inert, because the idle timer is only ever READ in the
      // PLAY arm of loop() and startGame() re-seeds it before PLAY resumes.
      lastInputMs = nowMs;

      // The WIN gate, above the START gate for the same reason that one sits above
      // the dispatch: a press in WIN must be CONSUMED, never fall through. Within
      // the lockout it is discarded outright; after it, it returns to START -- and
      // in neither case may it also start a game, cycle a color or steal.
      //
      // b.lastReleased and b.lastChangeMs were updated above, BEFORE this gate,
      // and that ordering is the whole reason the lockout works. A press swallowed
      // during the lockout is not re-detected as a fresh edge when the lockout
      // expires: no further edge exists while the button stays down. So a finger
      // resting on the winning button does not skip the screen -- it waits out the
      // unattended timeout, which is the right reading of a held button.
      if (mode == MODE_WIN) {
        if (nowMs - winEnteredMs >= WIN_LOCKOUT_MS) enterStart();
        return;
      }

      // The START gate. It sits ABOVE the action dispatch on purpose. Returning
      // here is what consumes the starting press -- it cannot also cycle a color
      // or launch a steal -- and b.lastReleased is already updated, so holding
      // the button does nothing further and the next press acts normally. Being
      // a self-contained block above the dispatch also keeps this independent of
      // whatever the four buttons happen to do in PLAY.
      if (mode == MODE_START) {
        startGame();
        return;
      }

      if (b.action == BTN_COLOR) {
        cycleColor(b.player);               // color only -- never moves a cube
      } else if (entangled()) {
        // Entangled: the direction button steals instead of steering, so a
        // player under the pull cannot aim their way out -- recoloring is the
        // only escape. Read live, with no latch, matching how every other
        // consumer of entangled() treats it.
        tryStealFlight(b.player);           // no velocity change either way
      } else {
        randomizeDirection(cubeFor(b.player));  // velocity only -- never recolors
      }
    }
  }
}

// ---- setup / loop ----------------------------------------------------------
static const uint32_t FRAME_MS = 25;  // physics + redraw cadence
static uint32_t lastFrameMs = 0;

// Draw one orbiting electron of `player` at perimeter slot i of n, in its own
// color, on the border one pixel outside the cube. Off-screen cells are skipped
// -- the electron still counts as held.
static void drawOrbitPixel(GFXcanvas8& c, const Cube& cube, int i, int n,
                           uint8_t color) {
  int x, y;
  orbitPos(cube, i, n, x, y);
  if (x < 0 || x >= W || y < 0 || y >= H) return;
  c.drawPixel(x, y, color);
}

static void render() {
  GFXcanvas8& c = panel.canvas;
  c.fillScreen(COMTEL_OFF);

  // The entanglement link: a line joining the two cubes' center pixels, shown
  // only while they share a color -- it is the tell that the pull is on. It is
  // drawn in that shared color (both cubes agree, so either will do), so the
  // linked pair reads as one connected object rather than two cubes plus a wire.
  // Drawn FIRST so every game object below paints over it: the canvas is opaque,
  // so painter's order alone keeps cubes and electrons in their own colors
  // wherever the line crosses them.
  if (entangled())
    c.drawLine(centerX(red), centerY(red), centerX(blue), centerY(blue),
               cubeColor(red));

  // Electrons in flight, each in its own color, drawn immediately after the link
  // so they read as riding on it -- and before the cubes, so the last pixel or
  // two of the trip passes BEHIND the thief's cube instead of punching a hole in
  // it. Capture is announced by the electron appearing in the orbit, not by its
  // final on-line pixel.
  for (uint8_t p = 0; p < 2; p++) {
    if (stealIdx[p] < 0) continue;
    int x, y;
    inFlightCell(p, x, y);
    if (x < 0 || x >= W || y < 0 || y >= H) continue;
    c.drawPixel(x, y, electrons[stealIdx[p]].color);
  }

  c.fillRect((int)floorf(red.px), (int)floorf(red.py), CUBE, CUBE, cubeColor(red));
  c.fillRect((int)floorf(blue.px), (int)floorf(blue.py), CUBE, CUBE, cubeColor(blue));

  // Floating electrons, each in its own color.
  for (int i = 0; i < N_ELECTRONS; i++)
    if (electrons[i].owner == -1)
      c.drawPixel((int)lroundf(electrons[i].x), (int)lroundf(electrons[i].y),
                  electrons[i].color);

  // Orbiting electrons: evenly spaced by count, tracking their owner's cube.
  const int nRed = orbitCount(0), nBlue = orbitCount(1);
  int iRed = 0, iBlue = 0;
  for (int i = 0; i < N_ELECTRONS; i++) {
    if (electrons[i].owner == 0)
      drawOrbitPixel(c, red, iRed++, nRed, electrons[i].color);
    else if (electrons[i].owner == 1)
      drawOrbitPixel(c, blue, iBlue++, nBlue, electrons[i].color);
  }

  panel.show();
}

// Show the start screen and stop playing. The image is a plain row-major byte
// per pixel and GFXcanvas8's buffer is the same, so the copy is the honest
// expression of "render it verbatim" -- no scaling, offset or palette remap. If
// a future GFX ever changes that layout the static_assert on size will not catch
// it; the equivalent nested drawPixel loop is the fallback.
//
// No teardown of the game state: nothing reads it while in START, and the next
// startGame() overwrites every field of both cubes and all fifteen electrons
// before anything is drawn again. Clearing it here would only create a second,
// weaker definition of "fresh" to drift out of step with that one.
static void enterStart() {
  memcpy(panel.canvas.getBuffer(), START_SCREEN, sizeof(START_SCREEN));
  panel.show();           // once -- service() keeps the latched frame lit
  mode = MODE_START;
}

// Show the win screen and end the game. Same shape as enterStart() on purpose:
// one memcpy, one show(), and no per-frame work thereafter -- two screens built
// two different ways would be the same feature implemented twice.
//
// enterWin() takes no winner argument and stores none. The win screen is a single
// image that does not say who won, so nothing downstream needs to know. If the
// artwork ever wants to name the winner, this is where the parameter goes and
// WIN_SCREEN becomes two arrays.
//
// NOTE while WIN_SCREEN is a copy of START_SCREEN: this transition is invisible on
// the glass. WIN is distinguishable from START only by behaviour -- it ignores
// buttons for WIN_LOCKOUT_MS, where START would have started a game at once.
static void enterWin() {
  memcpy(panel.canvas.getBuffer(), WIN_SCREEN, sizeof(WIN_SCREEN));
  panel.show();           // once -- service() keeps the latched frame lit
  mode = MODE_WIN;
  winEnteredMs = millis();
}

// Build a whole new game and start playing. Every path into PLAY comes through
// here -- boot has no other one -- so "a restarted game is indistinguishable
// from a cold boot" is true by construction rather than by two code paths
// agreeing with each other.
static void startGame() {
  // Start the cubes in opposite halves so they begin non-overlapping. Colors are
  // independent random draws -- the two cubes may legitimately start the same.
  red.colorIdx = random(0, N_COLORS);
  red.px = random(0, W / 2 - CUBE + 1);
  red.py = random(0, H - CUBE + 1);
  blue.colorIdx = random(0, N_COLORS);
  blue.px = random(W / 2, W - CUBE + 1);
  blue.py = random(0, H - CUBE + 1);
  randomizeMotion(red);
  randomizeMotion(blue);

  // Electrons: all 15 float at random empty cells (avoiding both cube start boxes
  // and already-placed floaters), each in a random color. None pre-orbit.
  int placed = 0;
  for (int i = 0; i < N_ELECTRONS; i++) {
    int ex = 0, ey = 0;
    for (int tries = 0; tries < 64; tries++) {
      ex = random(0, W);
      ey = random(0, H);
      if (within1px(cellX(red), cellY(red), CUBE, CUBE, ex, ey, 1, 1)) continue;
      if (within1px(cellX(blue), cellY(blue), CUBE, CUBE, ex, ey, 1, 1)) continue;
      bool clash = false;
      for (int j = 0; j < placed; j++)
        if ((int)electrons[j].x == ex && (int)electrons[j].y == ey) { clash = true; break; }
      if (!clash) break;
    }
    electrons[placed++] = {(float)ex, (float)ey, 0.0f, 0.0f, 0.0f, -1,
                           CYCLE[random(0, N_COLORS)]};
  }

  // The game state that does NOT live in a Cube or an Electron, and so is the
  // easy half to forget on a restart. Both steal slots must be cleared to keep
  // the stealIdx/owner invariant: every electron was just re-created floating,
  // so a slot left set would claim one that is not in flight.
  stealIdx[0]  = stealIdx[1]  = -1;
  stealDist[0] = stealDist[1] = 0.0f;
  orbitPhase = 0.0f;

  // lastFrameMs matters more than it looks: left stale from before an idle
  // stretch, the first frame of the new game would integrate a multi-second dt
  // and teleport both cubes across the panel.
  lastFrameMs = millis();
  lastInputMs = millis();
  mode = MODE_PLAY;
  render();
}

void setup() {
  Serial.begin(115200);
  panel.begin();
  randomSeed(esp_random());
  configureButtons(millis());
  enterStart();   // no game exists until a button asks for one
}

void loop() {
  panel.service();  // keep the display multiplexed every pass

  pollButtons(millis());  // poll every pass so presses aren't missed

  // In START there is nothing to advance and nothing to redraw -- the image is
  // already latched on the glass and service() above is all it needs.
  if (mode == MODE_START) return;

  // Read the clock AFTER polling, not before. A press may have just called
  // startGame(), which re-seeds lastFrameMs and lastInputMs from a later
  // millis() than a pre-poll reading; both comparisons below are unsigned, so a
  // `now` that predates them underflows to ~49 days and would fire the idle
  // timeout on the game's very first pass.
  const uint32_t now = millis();

  // In WIN, as in START, there is nothing to advance and nothing to redraw. The
  // only question is whether the screen has stood unattended long enough to give
  // up on a press -- a dismissing press has already been handled by pollButtons
  // above and left via the START gate, so reaching here means nobody pressed.
  //
  // This sits ABOVE the frame gate so the return is prompt rather than quantised
  // to FRAME_MS, at the cost of one unsigned subtract per pass. (It cannot sit
  // above the `now` read: it needs the same post-poll clock, for the same
  // underflow reason documented there.)
  if (mode == MODE_WIN) {
    if (now - winEnteredMs >= WIN_LOCKOUT_MS + WIN_HOLD_MS) enterStart();
    return;
  }

  if (now - lastFrameMs < FRAME_MS) return;
  const float dt = (now - lastFrameMs) / 1000.0f;
  lastFrameMs = now;

  // Idle timeout. Unsigned subtraction makes this millis()-rollover-safe, the
  // same idiom as the frame gate above and the button debounce. Checked once a
  // frame rather than every pass, so it can land up to 25 ms late on a 30 s
  // timer -- 0.08%, and it keeps the comparison off the hot polling path.
  if (now - lastInputMs >= IDLE_TIMEOUT_MS) {
    enterStart();   // abandon the game outright; the next press builds a new one
    return;
  }

  applyAttraction(dt);                     // rubber band, before integrating so
  integrateAndBounce(red, dt);             // this frame already uses the new
  integrateAndBounce(blue, dt);            // velocity (semi-implicit Euler)
  if (cubesOverlap()) resolveCollision();  // may release all electrons
  updateSteals(dt);                        // after the collision, so a same-color
                                           // wipe cancels a landing steal; before
                                           // integrate, so a forfeited electron
                                           // starts moving this same frame
  integrateElectrons(dt);                  // advance any that were flung out
  autoCollect(0);                          // run-over pickup, color-matched
  autoCollect(1);

  orbitPhase += ORBIT_STEP;                // slow border rotation (cells/frame)
  if (orbitPhase >= ORBIT_PERIM) orbitPhase -= ORBIT_PERIM;

  // WIN TEST. Asked once, here, from the state -- after everything that can move
  // an electron this frame and before the frame is drawn. Three things about that
  // placement are load-bearing:
  //
  //  * ASKING FROM THE STATE beats asking from the events. Only autoCollect and a
  //    steal capture can produce a win today (release and forfeit only ever
  //    reduce a holding), but a flag set from inside those two would have to be
  //    re-audited every time an electron learns a new way to change hands. A
  //    15-iteration count at 40 Hz costs nothing and cannot fall out of date.
  //
  //  * THE WINNING FRAME IS NOT RENDERED. Returning before render() means the
  //    glass goes from the previous game frame straight to the win image. Drawing
  //    the winning orbit for one 25 ms frame first would read as a flicker, not
  //    a victory lap; if a lap is ever wanted it should be a deliberate feature.
  //
  //  * A DRAW IS IMPOSSIBLE, so the order of the two tests is arbitrary and safe.
  //    With 15 electrons, both players cannot hold 8 at once.
  //
  // The idle timeout above wins a same-frame tie with this test, deliberately: it
  // returns before the physics block, so a game already abandoned cannot win on
  // the frame it dies. Only autoCollect can gain an electron without a press, so
  // that tie needs a cube drifting onto the ninth electron at second 30 -- and a
  // win screen shown to an empty room is worse than a missed win.
  if (orbitCount(0) == WIN_ELECTRONS || orbitCount(1) == WIN_ELECTRONS) {
    enterWin();
    return;
  }

  render();
}
