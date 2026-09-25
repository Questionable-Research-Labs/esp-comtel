// ============================================================================
//  win_screen_image.h -- the entanglement plugin's static win screen, shown when
//  one player has collected all nine electrons.
//
//  PLACEHOLDER ARTWORK, AND IT IS A DELIBERATE COPY. The pixel data below is
//  byte-for-byte the current contents of START_SCREEN in start_screen_image.h.
//  That duplication is intentional and temporary -- the win screen's own artwork
//  is still to be supplied -- so do NOT "fix" it by including the start-screen
//  header or aliasing its array. The two screens are separate data precisely so
//  that replacing this one is a self-contained edit.
//
//  CONSEQUENCE WHILE THE COPY STANDS: the panel cannot tell you which mode the
//  plugin is in, because WIN and START look identical. They differ only in
//  behaviour -- WIN ignores every button for its first 2 s and returns to START
//  on a press after that (or unattended at 7 s), whereas a press in START starts
//  a game immediately. If the mode machine ever needs debugging on the glass,
//  temporarily changing a few values here is the quickest way to tell them apart.
//
//  LAYOUT: 16 rows of 96 entries, row-major. WIN_SCREEN[y][x] is the panel pixel
//  at column x (0 = left), row y (0 = top). Both dimensions must match the panel
//  exactly -- the static_assert below enforces it, so a mis-sized paste is a
//  compile error rather than a garbled picture.
//
//  VALUES: one per pixel, written as the bare palette number for legibility at
//  96 columns wide. They are the COMTEL_* constants from comtel_panel.h:
//
//      0 = COMTEL_OFF      1 = COMTEL_RED      2 = COMTEL_GREEN
//      3 = COMTEL_YELLOW   4 = COMTEL_BLUE     5 = COMTEL_MAGENTA
//      6 = COMTEL_CYAN     7 = COMTEL_WHITE
//
//  The static_assert under the legend pins those numbers to the named constants,
//  so if the palette is ever renumbered this file stops compiling instead of
//  quietly changing colour.
//
//  Data only -- no logic lives here.
// ============================================================================
#pragma once
#include <Arduino.h>
#include "../driver/comtel_panel.h"

static_assert(COMTEL_OFF   == 0 && COMTEL_RED  == 1 && COMTEL_GREEN   == 2 &&
              COMTEL_YELLOW== 3 && COMTEL_BLUE == 4 && COMTEL_MAGENTA == 5 &&
              COMTEL_CYAN  == 6 && COMTEL_WHITE== 7,
              "win screen pixels are written as bare palette numbers; the "
              "COMTEL_* values they stand for have changed");

static const uint8_t WIN_SCREEN[ComtelPanel::H][ComtelPanel::W] = {
  {0,0,2,2,0,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,0,0,4,4,4,4,4,4,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,2,2,0,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,4,4,0,4,4,0,0,0,1,0,4,0,4,4,4,4,4,0,4,4,4,4,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,2,2,2,0,0,0,2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,4,4,0,0,4,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {2,2,2,2,0,0,0,2,2,0,2,2,2,2,2,0,0,2,2,0,0,0,0,2,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,4,0,0,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,2,2,2,0,0,0,2,2,0,2,2,2,2,2,2,2,2,0,2,2,2,2,2,2,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,1,1,0,0,0,1,1,0,0,0,0,0,4,0,0,0,0,0,4,4,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,2,2,0,2,2,2,2,0,2,0,0,0,0,2,2,0,0,2,2,2,2,0,2,2,0,1,1,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,1,1,1,0,1,1,0,0,0,0,0,4,0,4,4,4,4,4,4,4,4,4,4,4,0,4,4,0,0,0,0,0},
  {0,0,2,2,2,2,2,2,2,0,2,2,0,0,0,2,0,0,0,0,0,0,0,0,2,2,0,1,1,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,1,1,0,1,0,0,1,1,0,0,0,0,0,4,0,4,4,4,4,0,0,0,4,4,4,4,0,4,4,0,0,0,0,0},
  {0,0,0,0,2,2,0,2,2,0,2,2,0,0,0,0,0,0,0,0,0,0,0,2,2,0,1,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,1,1,0,1,0,0,1,1,0,0,0,0,0,4,0,4,4,0,0,0,0,0,0,4,4,4,0,0,4,4,0,0,0,0},
  {0,0,0,0,2,2,0,2,0,0,2,2,0,0,0,0,0,0,0,0,0,0,0,2,2,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,1,1,0,1,0,1,1,0,0,0,0,0,0,4,0,4,4,0,0,0,0,0,0,0,4,4,0,0,4,4,0,0,0,0},
  {0,0,0,0,2,2,0,0,0,2,2,2,0,0,0,0,0,0,0,0,0,0,0,2,2,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,1,1,0,1,0,1,1,0,0,0,0,0,0,4,0,4,4,0,0,0,0,0,0,0,0,4,4,0,4,4,0,0,0,0},
  {0,0,0,0,2,2,0,0,0,2,2,2,0,0,0,0,0,0,0,0,0,0,0,2,2,0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,1,1,1,1,0,0,0,0,0,0,4,0,4,4,0,0,0,0,0,0,0,0,4,4,0,4,4,0,0,0,0},
  {0,0,0,0,0,2,2,0,0,2,2,2,0,0,0,0,0,0,0,0,0,0,0,2,2,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,1,1,1,0,0,0,0,0,0,4,0,4,4,0,0,0,0,0,0,0,0,4,4,0,4,4,0,0,0,0},
  {0,0,0,0,0,2,2,0,0,2,2,2,2,2,2,2,2,0,0,0,0,0,0,2,2,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,4,0,4,4,0,0,0,0,0,0,0,0,4,4,0,4,4,0,0,0,0},
  {0,0,0,0,0,2,2,0,0,0,2,2,2,2,2,2,2,2,2,2,2,2,0,0,2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,0,4,4,0,0,0,0,0,0,0,0,0,4,4,4,0,0,0,0,0},
  {0,0,0,0,0,2,0,0,0,0,0,0,0,0,0,0,2,2,2,2,2,2,2,2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,4,4,0,0,0,0,4,4,4,4,4,4,4,4,4,0,0,0,4,4,4,4,4,4,4,4,0,4,4,0},
  {0,0,0,0,2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,0,0,0,0,0,0,4,0},
};


// The plugin memcpy()s this straight into the canvas, so the array must be
// exactly one byte per panel pixel with no padding.
static_assert(sizeof(WIN_SCREEN) == ComtelPanel::W * ComtelPanel::H,
              "WIN_SCREEN must be exactly 16 rows of 96 pixels");
