// ============================================================================
//  start_screen_image.h -- the entanglement plugin's static start screen.
//
//  PLACEHOLDER ARTWORK. Everything below the header comment is meant to be
//  replaced wholesale by the supplied 16 x 96 image; nothing in the plugin's
//  logic needs to change when it is. The pattern currently here is a test card
//  -- a one-pixel white border with a shallow red/green/blue diagonal running
//  top-left to bottom-right -- picked so that a transposed, mirrored or
//  off-by-one render is obvious the moment it hits the glass. The colour order
//  along the diagonal (red, then green, then blue, left to right) is what gives
//  away a horizontal flip.
//
//  LAYOUT: 16 rows of 96 entries, row-major. START_SCREEN[y][x] is the panel
//  pixel at column x (0 = left), row y (0 = top). Both dimensions must match
//  the panel exactly -- the static_assert below enforces it, so a mis-sized
//  paste is a compile error rather than a garbled picture.
//
//  VALUES: one per pixel, written as the bare palette number for legibility at
//  96 columns wide. They are the COMTEL_* constants from comtel_panel.h:
//
//      0 = COMTEL_OFF      1 = COMTEL_RED      2 = COMTEL_GREEN
//      3 = COMTEL_YELLOW   4 = COMTEL_BLUE     5 = COMTEL_MAGENTA
//      6 = COMTEL_CYAN     7 = COMTEL_WHITE
//
//  The static_asserts under the legend pin those numbers to the named
//  constants, so if the palette is ever renumbered this file stops compiling
//  instead of quietly changing colour.
//
//  Data only -- no logic lives here.
// ============================================================================
#pragma once
#include <Arduino.h>
#include "../driver/comtel_panel.h"

static_assert(COMTEL_OFF   == 0 && COMTEL_RED  == 1 && COMTEL_GREEN   == 2 &&
              COMTEL_YELLOW== 3 && COMTEL_BLUE == 4 && COMTEL_MAGENTA == 5 &&
              COMTEL_CYAN  == 6 && COMTEL_WHITE== 7,
              "start screen pixels are written as bare palette numbers; the "
              "COMTEL_* values they stand for have changed");

static const uint8_t START_SCREEN[ComtelPanel::H][ComtelPanel::W] = {
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,2,2,2,2,0,2,2,2,2,2,2,0,2,2,2,2,0,0,0,0,0,0,0,0,2,0,0,0,0,1,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,4,4,4,4,4,4,0,0,0,0,0,0,0,2,2,2,0,0,2,2,0,2,0,2,2,0,0,2,2,0,2,2,2,2,2,0,2,2,2,0,0,2,2,0,1,1,1,1,1,1,1},
  {0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,1,1,0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,4,4,0,0,0,0,0,0,0,4,4,4,0,2,2,2,2,0,2,2,0,0,2,0,2,2,0,0,2,2,0,2,0,0,0,0,0,2,0,2,2,0,0,2,0,1,0,0,1,1,0,1},
  {0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,1,1,1,1,0,0,1,1,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,4,4,0,0,0,0,0,0,0,0,4,4,0,2,0,0,0,0,0,2,2,0,2,0,2,2,2,0,2,2,0,0,2,2,2,2,0,2,0,2,2,0,0,2,0,1,0,0,1,1,0,1},
  {0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,1,1,1,1,0,0,1,1,0,0,1,1,1,0,1,1,0,0,4,4,4,4,4,4,4,0,4,4,0,4,4,0,0,4,4,4,0,0,0,4,4,0,2,0,0,0,0,0,2,2,0,2,2,0,2,2,0,2,2,0,0,2,2,2,2,0,2,0,2,2,0,0,2,0,0,0,1,1,1,0,0},
  {0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,1,1,0,1,0,0,1,1,0,0,1,1,1,0,0,0,0,0,0,0,4,4,0,0,4,0,4,4,0,4,4,0,0,4,4,4,4,4,0,4,4,0,2,2,2,2,2,0,0,2,0,0,2,0,2,2,0,2,2,0,2,2,0,0,0,2,2,0,0,2,2,0,2,0,0,0,1,1,0,0,0},
  {0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,1,1,0,1,1,0,1,1,0,0,1,1,1,0,4,4,4,4,4,0,4,4,0,4,4,0,4,4,0,0,4,4,0,0,4,0,4,4,0,4,4,2,2,2,2,2,2,0,0,2,2,0,2,0,0,2,0,2,2,0,2,2,0,0,0,2,2,0,0,2,2,0,2,0,0,0,1,1,0,0,0},
  {0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,1,1,0,1,1,0,1,1,0,0,1,1,1,4,4,0,0,4,4,0,4,4,0,4,4,0,0,4,0,0,4,4,0,0,4,0,4,0,4,4,0,2,2,0,0,0,0,0,2,2,0,0,2,2,0,2,0,2,2,2,2,0,0,0,0,2,2,0,0,2,2,0,2,0,0,0,1,1,0,0,0},
  {0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,1,1,0,1,1,0,1,1,0,0,1,1,1,4,4,0,0,4,4,0,4,4,0,0,4,4,0,4,0,0,4,4,0,0,4,0,0,0,4,4,0,2,2,0,0,0,0,0,2,2,0,0,2,2,2,0,0,2,2,0,2,2,2,2,2,2,2,0,0,2,2,0,2,0,0,0,1,1,0,0,0},
  {0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,1,1,0,0,1,0,1,1,0,0,1,1,1,4,4,0,0,4,4,0,4,4,0,0,4,4,0,4,0,4,4,0,0,0,4,0,0,4,4,0,0,0,2,2,2,2,2,0,2,2,0,0,0,2,0,0,0,0,0,2,0,2,2,2,2,0,2,0,0,2,2,0,2,0,0,0,1,1,0,0,0},
  {0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1,0,0,1,1,1,4,4,4,4,4,4,0,4,4,0,0,4,4,4,4,0,4,4,4,0,4,4,0,0,4,4,4,4,4,4,4,2,2,2,0,2,2,0,0,0,0,0,0,0,0,2,2,0,0,0,0,0,2,2,0,2,2,0,2,2,0,0,0,1,1,0,0,0},
  {0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,0,1,1,0,0,1,1,1,0,4,4,4,4,4,0,4,4,0,0,4,4,4,0,0,0,4,4,4,4,4,0,0,4,4,4,4,4,4,4,0,0,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,2,0,0,2,0,0,2,2,0,0,0,1,1,0,0,0},
  {0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,1,1,0,0,4,0,0,4,4,0,4,4,0,0,0,4,4,0,0,0,0,0,4,0,0,0,0,0,0,0,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,2,0,2,2,0,0,2,0,0,0,0,1,1,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,4,0,0,4,4,4,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,2,0,2,2,0,2,2,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,2,0,2,2,0,0,2,2,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,2,0,2,2,2,2,2,0,0,0,0,0,0,0,0,0},
};

// The plugin memcpy()s this straight into the canvas, so the array must be
// exactly one byte per panel pixel with no padding.
static_assert(sizeof(START_SCREEN) == ComtelPanel::W * ComtelPanel::H,
              "START_SCREEN must be exactly 16 rows of 96 pixels");
