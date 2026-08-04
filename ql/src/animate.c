/* animate.c -- frame-driven fall/flash animation, ported from game.js's
 * runSettleAnimated (FALL_MS_PER_ROW/FLASH_MS_PER_TOGGLE -> frame counts
 * at 50Hz). Only ever touches the specific cells that are moving or
 * flashing, never the whole board -- see animate.h. */
#include "render.h"
#include "animate.h"
#include "sound.h"

extern void wait_vbl(void);
extern void blit_tile(int col, int row, int sprite_index);
extern void clear_tile(int col, int row);
extern void blit_tile_y(int col, int pixel_y, int sprite_index);
extern void clear_tile_y(int col, int pixel_y);

/* game8.asm's note pitches (BASIC pitch + 1) as a scale reference: a
 * short, high "pop" on every match, close to n_g4=20. */
#define MATCH_POP_PITCH 20
#define MATCH_POP_DURATION 0x0300

/* Constant-speed fall (the old FALL_PX_PER_FRAME=4 every frame) looked
 * mechanical on long drops: real gravity accelerates, and a fixed
 * speed makes a multi-row fall feel like it's sliding at a fixed rate
 * rather than falling. Ease in from FALL_START_PX_PER_FRAME up to
 * FALL_MAX_PX_PER_FRAME, gaining FALL_ACCEL_PX_PER_FRAME every frame --
 * short (1-row) falls barely reach top speed so they still feel about
 * like before, long falls now visibly speed up instead of crawling at
 * a constant rate. */
#define FALL_START_PX_PER_FRAME 2
#define FALL_ACCEL_PX_PER_FRAME 1
#define FALL_MAX_PX_PER_FRAME 10

/* game.js: FLASH_MS_PER_TOGGLE=90, FLASH_TOGGLES=4. 4 frames/toggle at
 * 50Hz = 80ms/toggle, same rounding logic as the fall speed above. */
#define FLASH_FRAMES_PER_TOGGLE 4
#define FLASH_TOGGLES 4

static void animate_gravity_wave(Grid grid) {
    Faller fallers[INNER_W * INNER_H];
    int cur_y[INNER_W * INNER_H];
    int n = compute_fallers(grid, fallers); /* also applies gravity to grid */
    int i, done;
    int speed = FALL_START_PX_PER_FRAME;

    if (n == 0) return;

    for (i = 0; i < n; i++) {
        cur_y[i] = MARGIN_Y + fallers[i].from_row * TILE_H;
    }

    do {
        wait_vbl();
        done = 1;
        for (i = 0; i < n; i++) {
            int target_y = MARGIN_Y + fallers[i].to_row * TILE_H;
            int sprite;

            if (cur_y[i] >= target_y) continue;

            sprite = fallers[i].type - BLOCK_TYPE_BASE;
            clear_tile_y(fallers[i].col, cur_y[i]);
            cur_y[i] += speed;
            if (cur_y[i] > target_y) cur_y[i] = target_y;
            blit_tile_y(fallers[i].col, cur_y[i], sprite);
            if (cur_y[i] < target_y) done = 0;
        }
        if (speed < FALL_MAX_PX_PER_FRAME) speed += FALL_ACCEL_PX_PER_FRAME;
    } while (!done);
}

static void animate_flash_wave(Grid grid, Cell *cells, int n) {
    int toggle, i, frame;

    play_tone(MATCH_POP_PITCH, MATCH_POP_DURATION);

    for (toggle = 0; toggle < FLASH_TOGGLES; toggle++) {
        int visible = (toggle % 2) == 0;

        for (i = 0; i < n; i++) {
            if (visible) {
                unsigned char v = grid[cells[i].row][cells[i].col];
                blit_tile(cells[i].col, cells[i].row, v - BLOCK_TYPE_BASE);
            } else {
                clear_tile(cells[i].col, cells[i].row);
            }
        }
        for (frame = 0; frame < FLASH_FRAMES_PER_TOGGLE; frame++) wait_vbl();
    }

    for (i = 0; i < n; i++) grid[cells[i].row][cells[i].col] = 0;
}

void settle_animated(Grid grid, int *score) {
    int chain = 1;
    for (;;) {
        Cell to_clear[INNER_H * INNER_W];
        int n;

        animate_gravity_wave(grid);

        n = find_matches(grid, to_clear);
        if (n == 0) break;

        *score += n * 10 * chain;
        chain++;

        animate_flash_wave(grid, to_clear, n);
    }
}
