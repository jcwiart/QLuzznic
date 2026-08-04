#ifndef ANIMATE_H
#define ANIMATE_H

#include "game.h"

/* Animated version of settle(): repeatedly runs a fall wave (blocks
 * slide smoothly to their post-gravity position, frame by frame) then,
 * if it produced a match, a flash wave (matched cells blink before
 * being cleared) -- exactly game.js's runSettleAnimated, but frame-
 * counted against real VBL sync instead of wall-clock ms. Only touches
 * the cells that actually move/flash each frame (not a full redraw):
 * game8.asm's measurements put a full 80-cell board redraw at 50Hz far
 * over the 68008's frame budget. */
void settle_animated(Grid grid, int *score);

#endif
