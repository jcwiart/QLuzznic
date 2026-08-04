#include "render.h"

extern void blit_tile(int col, int row, int sprite_index);
extern void clear_tile(int col, int row);
extern void blit_steel_full(int col, int row);
extern void blit_steel_half(int col, int y_pixel);
extern void blit_steel_vert(int x_pixel, int row);
extern void blit_steel_corner(int x_pixel, int y_pixel);
extern void draw_cursor_frame(int col, int row, int frame);

void draw_board(Grid grid) {
    int r, c;
    for (r = 0; r < OUTER_H; r++) {
        for (c = 0; c < OUTER_W; c++) {
            redraw_cell(grid, r, c);
        }
    }
}

void redraw_cell(Grid grid, int row, int col) {
    unsigned char v = grid[row][col];
    int is_top_bottom = (row == 0 || row == OUTER_H - 1);
    int is_left_right = (col == 0 || col == OUTER_W - 1);

    /* Every border cell is always STEEL by construction (empty_grid()
     * in game.c) -- no need to check v for any of these three cases. */
    if (is_top_bottom && is_left_right) {
        int x = (col == 0) ? LEFT_BORDER_X : RIGHT_BORDER_X;
        int y = (row == 0) ? HUD_HEIGHT : BOTTOM_BORDER_Y;
        blit_steel_corner(x, y);
    } else if (is_top_bottom) {
        int y = (row == 0) ? HUD_HEIGHT : BOTTOM_BORDER_Y;
        blit_steel_half(col, y);
    } else if (is_left_right) {
        int x = (col == 0) ? LEFT_BORDER_X : RIGHT_BORDER_X;
        blit_steel_vert(x, row);
    } else if (v == STEEL) {
        blit_steel_full(col, row);
    } else if (v == JOKER) {
        blit_tile(col, row, JOKER_SPRITE_INDEX);
    } else if (v >= BLOCK_TYPE_BASE) {
        blit_tile(col, row, v - BLOCK_TYPE_BASE);
    } else {
        clear_tile(col, row);
    }
}

void draw_cursor(int row, int col, int frame) {
    draw_cursor_frame(col, row, frame);
}
