/* host_test.c -- native sanity checks for game.c, compiled with the
 * system compiler (not qcc) so we get fast feedback without needing to
 * debug inside the QL emulator, where there's no console once the
 * hardware takeover has happened. Not a QL build artifact. */
#include <stdio.h>
#include <string.h>
#include "../src/game.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
    else { printf("ok:   %s\n", msg); } \
} while (0)

static void print_grid(Grid grid) {
    int r, c;
    for (r = 0; r < OUTER_H; r++) {
        for (c = 0; c < OUTER_W; c++) {
            unsigned char v = grid[r][c];
            if (v == STEEL) printf(" S");
            else if (v == 0) printf(" .");
            else printf("%2d", v);
        }
        printf("\n");
    }
}

/* A hand-built level: a colour-1 pair (rows 0-1, col 0) already
 * vertically adjacent -- should merge on settle -- a lone colour 2
 * that becomes an orphan, and one internal steel cell (row0, col6)
 * splitting column 6 into two segments for the segment_bounds test.
 * Expected outcomes below were traced by hand cell-by-cell; see the
 * comments at each assertion. */
static LevelDef test_level = {
    {
        /* row0 */ {1, 0, 0, 0, 0, 0, 9, 0},
        /* row1 */ {1, 0, 0, 0, 0, 0, 0, 0},
        /* row2 */ {0, 0, 2, 0, 0, 0, 0, 0},
        /* row3 */ {0, 0, 0, 0, 0, 0, 0, 0},
        /* row4 */ {0, 0, 0, 0, 0, 0, 0, 0},
        /* row5 */ {0, 0, 0, 0, 0, 0, 0, 0},
    }
};

static void test_build_level(void) {
    Grid grid;
    int counts[STANDARD_BLOCK_COUNT];

    build_level(&test_level, grid);
    printf("-- after build_level --\n");
    print_grid(grid);

    /* first-appearance order in row-major scan: colour 1 (row0,col0),
     * then colour 9=steel (skipped, not a colour), then colour 2
     * (row2,col2). So 1->10, 2->11. */
    CHECK(grid[BORDER + 0][BORDER + 0] == BLOCK_TYPE_BASE + 0, "colour 1 remapped to sprite 0");
    CHECK(grid[BORDER + 1][BORDER + 0] == BLOCK_TYPE_BASE + 0, "colour 1's second cell remapped the same way");
    CHECK(grid[BORDER + 2][BORDER + 2] == BLOCK_TYPE_BASE + 1, "colour 2 remapped to sprite 1");
    CHECK(grid[BORDER + 0][BORDER + 6] == STEEL, "steel cell preserved");
    CHECK(grid[0][0] == STEEL && grid[OUTER_H - 1][OUTER_W - 1] == STEEL, "outer border is steel");

    counts_by_type(grid, counts);
    CHECK(counts[0] == 2, "two sprite-0 blocks before settle");
    CHECK(counts[1] == 1, "one sprite-1 block (orphan) before settle");
}

static void test_gravity_and_settle(void) {
    Grid grid;
    int score = 0;
    int cleared;

    build_level(&test_level, grid);
    cleared = settle(grid, &score);
    printf("-- after settle: cleared=%d score=%d --\n", cleared, score);
    print_grid(grid);

    /* The two sprite-0 blocks (col BORDER+0, interior rows 0-1) are
     * already vertically adjacent -- gravity drops them to the bottom
     * of their (steel-free) column still touching at rows 5-6, so they
     * match and clear. The lone sprite-1 block falls alone in its own
     * column to the bottom and stays (a group of 1 never matches). */
    CHECK(cleared == 2, "settle clears exactly the sprite-0 pair");
    CHECK(score == 20, "score = 2 cells * 10 * chain(1)");
    CHECK(grid[PLAY_MAX_ROW][BORDER + 0] == 0, "sprite-0 column now empty after clearing");
    CHECK(grid[PLAY_MAX_ROW][BORDER + 2] == BLOCK_TYPE_BASE + 1,
          "lone sprite-1 (orphan) fell to the bottom of its column, unmatched");
}

static void test_orphan_and_empty(void) {
    Grid grid;
    int score = 0;

    build_level(&test_level, grid);
    settle(grid, &score);

    CHECK(has_orphan_block(grid) == 1, "lone sprite-1 block is detected as an orphan");
    CHECK(is_grid_empty(grid) == 0, "grid is not empty (the orphan block remains)");

    /* Clear everything by hand and confirm is_grid_empty flips. */
    {
        int r, c;
        for (r = PLAY_MIN_ROW; r <= PLAY_MAX_ROW; r++)
            for (c = PLAY_MIN_COL; c <= PLAY_MAX_COL; c++)
                grid[r][c] = 0;
    }
    CHECK(is_grid_empty(grid) == 1, "grid reports empty once cleared");
    CHECK(has_orphan_block(grid) == 0, "no orphan on an empty grid");
}

static void test_segment_bounds(void) {
    Grid grid;
    int top, bottom;
    build_level(&test_level, grid);

    /* Interior col6 -> grid col BORDER+6=7 has a steel cell at interior
     * row0 (grid row1). segment_bounds for a row below it should stop
     * right after the steel, not run to the play field top. */
    segment_bounds(grid, BORDER + 6, PLAY_MAX_ROW, &top, &bottom);
    printf("-- segment_bounds col=%d row=%d -> top=%d bottom=%d --\n", BORDER + 6, PLAY_MAX_ROW, top, bottom);
    CHECK(top == BORDER + 1, "segment below the steel starts right after it");
    CHECK(bottom == PLAY_MAX_ROW, "segment below the steel runs to the play field bottom");
}

static void test_has_any_move(void) {
    Grid grid;
    empty_grid(grid);
    CHECK(has_any_move(grid) == 0, "an all-empty interior has no block to move");

    grid[PLAY_MIN_ROW][PLAY_MIN_COL] = BLOCK_TYPE_BASE;
    CHECK(has_any_move(grid) == 1, "a lone block next to empty space can move");
}

static void test_move_block(void) {
    Grid grid;
    int target_col, landing_row, moved;

    empty_grid(grid);
    grid[PLAY_MIN_ROW][PLAY_MIN_COL] = BLOCK_TYPE_BASE;

    moved = move_block(grid, PLAY_MIN_ROW, PLAY_MIN_COL, 1, &target_col);
    CHECK(moved == 1, "move into an empty adjacent cell succeeds");
    CHECK(target_col == PLAY_MIN_COL + 1, "target_col reported correctly");
    CHECK(grid[PLAY_MIN_ROW][PLAY_MIN_COL] == 0, "source cell now empty");
    CHECK(grid[PLAY_MIN_ROW][PLAY_MIN_COL + 1] == BLOCK_TYPE_BASE, "block relocated");

    /* the block is now at PLAY_MIN_COL+1; PLAY_MIN_COL (its old spot) is
     * empty, so asking to move FROM there must be rejected. */
    moved = move_block(grid, PLAY_MIN_ROW, PLAY_MIN_COL, -1, &target_col);
    CHECK(moved == 0, "no block at the source cell: move rejected");
    CHECK(grid[PLAY_MIN_ROW][PLAY_MIN_COL + 1] == BLOCK_TYPE_BASE, "grid unchanged by the rejected move");

    /* occupied target is rejected */
    grid[PLAY_MIN_ROW][PLAY_MIN_COL + 2] = BLOCK_TYPE_BASE + 1;
    moved = move_block(grid, PLAY_MIN_ROW, PLAY_MIN_COL + 1, 1, &target_col);
    CHECK(moved == 0, "moving onto an occupied cell is rejected");
    CHECK(grid[PLAY_MIN_ROW][PLAY_MIN_COL + 1] == BLOCK_TYPE_BASE, "rejected move leaves grid unchanged");

    /* cursor_landing_row: drop a block into a column above another block
     * and confirm settle() + landing_row put it right on top. */
    empty_grid(grid);
    grid[PLAY_MAX_ROW][PLAY_MIN_COL + 3] = BLOCK_TYPE_BASE + 2; /* already resting at the bottom */
    grid[PLAY_MIN_ROW][PLAY_MIN_COL + 2] = BLOCK_TYPE_BASE + 3; /* about to slide in beside it, at the top */
    moved = move_block(grid, PLAY_MIN_ROW, PLAY_MIN_COL + 2, 1, &target_col);
    CHECK(moved == 1, "slide into the column above the resting block");
    {
        int score = 0;
        settle(grid, &score);
    }
    landing_row = cursor_landing_row(grid, target_col, PLAY_MIN_ROW);
    printf("-- cursor_landing_row after settle: %d (expect %d) --\n", landing_row, PLAY_MAX_ROW - 1);
    CHECK(landing_row == PLAY_MAX_ROW - 1, "moved block fell to rest just above the one already at the bottom");
}

static void test_compute_fallers(void) {
    Grid grid;
    Faller fallers[INNER_W * INNER_H];
    int n, i;
    int found_a = 0, found_b = 0;

    empty_grid(grid);
    grid[PLAY_MIN_ROW][PLAY_MIN_COL] = BLOCK_TYPE_BASE;         /* "A" */
    grid[PLAY_MIN_ROW + 2][PLAY_MIN_COL] = BLOCK_TYPE_BASE + 1; /* "B", with a gap above it */

    n = compute_fallers(grid, fallers);
    printf("-- compute_fallers: %d fallers --\n", n);
    for (i = 0; i < n; i++) {
        printf("   col=%d from=%d to=%d type=%d\n", fallers[i].col, fallers[i].from_row, fallers[i].to_row, fallers[i].type);
    }
    CHECK(n == 2, "both blocks reported as fallers");
    for (i = 0; i < n; i++) {
        if (fallers[i].type == BLOCK_TYPE_BASE) {
            CHECK(fallers[i].from_row == PLAY_MIN_ROW && fallers[i].to_row == PLAY_MAX_ROW - 1,
                  "block A falls from its start row to the second-to-last row");
            found_a = 1;
        } else if (fallers[i].type == BLOCK_TYPE_BASE + 1) {
            CHECK(fallers[i].from_row == PLAY_MIN_ROW + 2 && fallers[i].to_row == PLAY_MAX_ROW,
                  "block B falls from its start row to the bottom");
            found_b = 1;
        }
    }
    CHECK(found_a && found_b, "both distinct block identities were tracked through the fall (not swapped)");
    CHECK(grid[PLAY_MAX_ROW - 1][PLAY_MIN_COL] == BLOCK_TYPE_BASE, "grid actually updated: A above B");
    CHECK(grid[PLAY_MAX_ROW][PLAY_MIN_COL] == BLOCK_TYPE_BASE + 1, "grid actually updated: B at the bottom");
}

static void test_all_levels_load_and_settle(void) {
    int i;
    CHECK(LEVEL_COUNT == 100, "level pack converted to exactly 100 QLuzznic levels");

    for (i = 0; i < LEVEL_COUNT; i++) {
        Grid grid;
        int score = 0;

        build_level(&LEVELS[i], grid);
        settle(grid, &score); /* must terminate -- an infinite chain would hang the test */
    }
    printf("-- all %d levels built + settled without hanging or crashing --\n", LEVEL_COUNT);
    CHECK(1, "all levels load and settle without crashing or looping forever");
}

/* Every level must still be winnable the instant it's loaded -- no
 * level should already be a dead end (has_orphan_block) or have zero
 * legal moves (has_any_move) before the player has touched anything.
 * This is exactly the class of bug found live on level 100 twice in a
 * row (see [[qluzznic-own-levels-and-joker]]): has_orphan_block not
 * knowing about jokers at all, then a fix that swung too far the
 * other way. Checking every level's *starting* position here would
 * have caught either mistake immediately instead of needing a
 * screenshot from real play to notice. */
static void test_no_level_starts_stuck(void) {
    int i;
    int stuck_count = 0;

    for (i = 0; i < LEVEL_COUNT; i++) {
        Grid grid;
        int score = 0;

        build_level(&LEVELS[i], grid);
        settle(grid, &score);

        if (is_grid_empty(grid)) continue; /* level solved itself on load -- fine */

        if (has_orphan_block(grid)) {
            printf("FAIL: level %d (displayed LEVEL %d) starts with more orphaned "
                   "colours than jokers to rescue them\n",
                   i, i + 1);
            stuck_count++;
        } else if (!has_any_move(grid)) {
            printf("FAIL: level %d (displayed LEVEL %d) starts with no legal move at all\n",
                   i, i + 1);
            stuck_count++;
        }
    }
    CHECK(stuck_count == 0, "no level's starting position is already a dead end");
}

int main(void) {
    test_build_level();
    test_gravity_and_settle();
    test_orphan_and_empty();
    test_segment_bounds();
    test_has_any_move();
    test_move_block();
    test_compute_fallers();
    test_all_levels_load_and_settle();
    test_no_level_starts_stuck();

    if (failures) {
        printf("\n%d CHECK(S) FAILED\n", failures);
        return 1;
    }
    printf("\nAll checks passed.\n");
    return 0;
}
