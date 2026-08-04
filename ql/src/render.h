#ifndef RENDER_H
#define RENDER_H

#include "game.h"

/* Must match asm/display.s's margin_x/margin_y/tile_w/tile_h equ values
 * exactly -- needed here for animation code that computes intermediate
 * pixel Y positions (blit_tile_y/clear_tile_y), not just whole grid
 * rows. Tiles are 24x32 (not square): mode 8 pixels are physically
 * wider than tall by 4:3 (the screen's own aspect, since its 256x256
 * pixel grid is square), so a tile needs the inverse 3:4 ratio to
 * appear square on screen. MARGIN_X is NOT the screen-edge margin (see
 * LEFT_BORDER_X/RIGHT_BORDER_X below for that) -- it's whatever makes
 * col*TILE_W+MARGIN_X land right for INTERIOR columns (1..INNER_W)
 * once the border columns are half-width; see asm/display.s's
 * margin_x comment for the derivation. MARGIN_Y=16 (not 0) is the
 * offset that makes MARGIN_Y+row*TILE_H land correctly for grid rows
 * 1..7 -- see the HUD band note below for why row 0 alone needs its
 * own fixed Y instead. */
#define MARGIN_X -4
#define MARGIN_Y 16
#define TILE_W 24
#define TILE_H 32

/* blocks.png tile 8 is the game's existing hand-drawn "magic" rainbow
 * tile (yellow-bordered diagonal stripes) -- already part of the
 * sprite sheet from the original art pass, just never wired to a
 * block type until the joker mechanic needed it. Tile 9 (the old
 * single-tile steel pattern, unused since steel switched to the
 * steel_sub mosaic in asm/display.s) stays as steel art, untouched. */
#define JOKER_SPRITE_INDEX 8

/* HUD rework: grid rows 0 and OUTER_H-1 are always steel for every
 * level (empty_grid() in game.c lays that border down unconditionally
 * before any level data is applied), so they're safe to shrink to
 * half height (16px) and hand the reclaimed 32px to a HUD band across
 * the very top of the screen (y=0..31). Border-row steel is drawn via
 * asm/display.s's blit_steel_half, a 16-row-tall strip of the SAME
 * existing 24x32 steel sprite blit_tile already uses everywhere else
 * -- see that function's comment for why it's a near-copy of the
 * already-proven blit_tile_y rather than a new smaller composited
 * tile (an earlier from-scratch attempt reliably crashed the job at
 * boot). Must match asm/display.s's hud_height/bottom_border_y equ
 * values exactly. */
#define HUD_HEIGHT 32       /* top HUD band height; also row 0's fixed pixel Y */
#define BOTTOM_BORDER_Y 240 /* MARGIN_Y + 7*TILE_H: row (OUTER_H-1)'s pixel Y */

/* Same idea, sideways: grid cols 0 and OUTER_W-1 are always steel too,
 * shrunk to half width (12px, border_col_w in asm/display.s) so the
 * interior can be 9 columns wide (INNER_W) instead of 8 while the
 * board's overall footprint stays 240px (8 + 12*2 + 9*24 = 248, plus
 * the matching 8px on the right = 256 -- same margins as before).
 * asm/display.s's blit_steel_vert (non-corner border-column cells)
 * and blit_steel_corner (the 4 corners, where a border row and column
 * coincide) take these as explicit pixel X, the same way
 * HUD_HEIGHT/BOTTOM_BORDER_Y are explicit pixel Y for border rows --
 * neither fits the general col/row formula once they're half-size.
 * Must match asm/display.s's left_border_x/right_border_x/
 * border_col_w equ values exactly. */
#define LEFT_BORDER_X 8
#define RIGHT_BORDER_X 236 /* left_border_x + border_col_w(12) + INNER_W*TILE_W */
#define BORDER_COL_W 12

/* Draws every non-empty cell of the board (blocks + steel) via
 * blit_tile. Empty cells are left as-is (background already black). */
void draw_board(Grid grid);

/* Redraws exactly what belongs at (row,col): the real block/steel
 * sprite, or a cleared (black) cell if empty. Used to erase overlays
 * (the cursor) without disturbing the rest of the board. */
void redraw_cell(Grid grid, int row, int col);

/* Number of frames in selection.png (must match tools/png2cursor.py's
 * output, which reads it straight from the image width). */
#define CURSOR_FRAME_COUNT 4

/* Draws the marching-ants cursor overlay (frame 0..CURSOR_FRAME_COUNT-1)
 * at (row,col): a masked blit that only touches the dashed outline
 * ring, leaving the real block/steel/empty content underneath visible
 * through the rest of the tile. */
void draw_cursor(int row, int col, int frame);

#endif
