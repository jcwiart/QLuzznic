/* game.c -- grid/rules engine, ported from game.js (applyGravity,
 * findMatches, settle, hasOrphanBlock, etc). See game.h. */
#include "game.h"

void empty_grid(Grid grid) {
    int r, c;
    for (r = 0; r < OUTER_H; r++) {
        for (c = 0; c < OUTER_W; c++) {
            grid[r][c] = 0;
        }
    }
    for (c = 0; c < OUTER_W; c++) {
        grid[0][c] = STEEL;
        grid[OUTER_H - 1][c] = STEEL;
    }
    for (r = 0; r < OUTER_H; r++) {
        grid[r][0] = STEEL;
        grid[r][OUTER_W - 1] = STEEL;
    }
}

void build_level(const LevelDef *level, Grid grid) {
    /* colours_used, in first-appearance row-major order -- matches
     * game.js's colorsUsed() (a Set preserves insertion order). */
    unsigned char remap[STANDARD_BLOCK_COUNT + 1]; /* index by original colour 1..8 */
    int seen_count = 0;
    int r, c;

    for (c = 0; c <= STANDARD_BLOCK_COUNT; c++) remap[c] = 0;

    for (r = 0; r < INNER_H; r++) {
        for (c = 0; c < INNER_W; c++) {
            unsigned char v = level->interior[r][c];
            if (v != 0 && v != STEEL && v != JOKER && remap[v] == 0 && seen_count < STANDARD_BLOCK_COUNT) {
                remap[v] = (unsigned char)(BLOCK_TYPE_BASE + seen_count);
                seen_count++;
            }
        }
    }

    empty_grid(grid);
    for (r = 0; r < INNER_H; r++) {
        for (c = 0; c < INNER_W; c++) {
            unsigned char v = level->interior[r][c];
            int gr = BORDER + r;
            int gc = BORDER + c;
            if (v == STEEL) {
                grid[gr][gc] = STEEL;
            } else if (v == JOKER) {
                grid[gr][gc] = JOKER;
            } else if (v != 0) {
                grid[gr][gc] = remap[v];
            }
        }
    }
}

void apply_gravity(Grid grid) {
    int c, r;
    for (c = PLAY_MIN_COL; c <= PLAY_MAX_COL; c++) {
        int seg_start = PLAY_MIN_ROW;
        for (r = PLAY_MIN_ROW; r <= PLAY_MAX_ROW + 1; r++) {
            int is_boundary = (r > PLAY_MAX_ROW) || (grid[r][c] == STEEL);
            int rr, write_row;
            unsigned char values[INNER_H];
            int n = 0;

            if (!is_boundary) continue;

            for (rr = seg_start; rr <= r - 1; rr++) {
                if (grid[rr][c] != 0) values[n++] = grid[rr][c];
            }
            write_row = r - 1;
            for (rr = n - 1; rr >= 0; rr--) {
                grid[write_row][c] = values[rr];
                write_row--;
            }
            while (write_row >= seg_start) {
                grid[write_row][c] = 0;
                write_row--;
            }
            seg_start = r + 1;
        }
    }
}

/* Ordinary same-colour groups (size >= 2), flood-filled. Jokers are
 * skipped here (like STEEL) -- they never join an ordinary group, only
 * find_joker_matches() below can clear one. */
static int find_ordinary_matches(Grid grid, Cell *out) {
    unsigned char seen[OUTER_H][GRID_STRIDE]; /* see game.h's GRID_STRIDE comment */
    Cell stack[INNER_H * INNER_W];
    Cell group[INNER_H * INNER_W];
    int r, c, out_n = 0;

    for (r = 0; r < OUTER_H; r++) {
        for (c = 0; c < OUTER_W; c++) seen[r][c] = 0;
    }

    for (r = PLAY_MIN_ROW; r <= PLAY_MAX_ROW; r++) {
        for (c = PLAY_MIN_COL; c <= PLAY_MAX_COL; c++) {
            unsigned char type = grid[r][c];
            int sp, group_n;

            if (type == 0 || type == STEEL || type == JOKER || seen[r][c]) continue;

            sp = 0;
            group_n = 0;
            stack[sp].row = (unsigned short)r;
            stack[sp].col = (unsigned short)c;
            sp++;
            seen[r][c] = 1;

            while (sp > 0) {
                int cr, cc;
                static const int dr[4] = { -1, 1, 0, 0 };
                static const int dc[4] = { 0, 0, -1, 1 };
                int k;

                sp--;
                cr = stack[sp].row;
                cc = stack[sp].col;
                group[group_n].row = (unsigned short)cr;
                group[group_n].col = (unsigned short)cc;
                group_n++;

                for (k = 0; k < 4; k++) {
                    int nr = cr + dr[k];
                    int nc = cc + dc[k];
                    /* the steel border is a natural sentinel: a block's
                     * type (10..17) never equals STEEL (9), so the
                     * flood-fill can never run off the play field. */
                    if (seen[nr][nc] || grid[nr][nc] != type) continue;
                    seen[nr][nc] = 1;
                    stack[sp].row = (unsigned short)nr;
                    stack[sp].col = (unsigned short)nc;
                    sp++;
                }
            }

            if (group_n >= 2) {
                int i;
                for (i = 0; i < group_n; i++) out[out_n++] = group[i];
            }
        }
    }
    return out_n;
}

/* Joker activation: a joker looks at its 4 orthogonal neighbours,
 * ignoring empty/STEEL/other-joker cells. If every ordinary-coloured
 * neighbour it sees is the SAME colour (at least one), the joker and
 * all of those neighbours are cleared. If it sees two different
 * colours, or no ordinary-coloured neighbour at all (including the
 * "two jokers side by side" case), it stays put. Every eligible joker
 * on the board activates in the same pass, mirroring
 * find_ordinary_matches()'s "clear everything found, all at once"
 * behaviour; a mark grid dedupes cells two different jokers might
 * both reach. Only ever called (via find_matches()) when
 * find_ordinary_matches() found nothing, per the game's rule that
 * ordinary matches always resolve first. */
static int find_joker_matches(Grid grid, Cell *out) {
    unsigned char mark[OUTER_H][GRID_STRIDE]; /* see game.h's GRID_STRIDE comment */
    static const int dr[4] = { -1, 1, 0, 0 };
    static const int dc[4] = { 0, 0, -1, 1 };
    int r, c, out_n = 0;

    for (r = 0; r < OUTER_H; r++) {
        for (c = 0; c < OUTER_W; c++) mark[r][c] = 0;
    }

    for (r = PLAY_MIN_ROW; r <= PLAY_MAX_ROW; r++) {
        for (c = PLAY_MIN_COL; c <= PLAY_MAX_COL; c++) {
            unsigned char neighbour_colour;
            int consistent, k;

            if (grid[r][c] != JOKER) continue;

            neighbour_colour = 0;
            consistent = 1;
            for (k = 0; k < 4; k++) {
                unsigned char v = grid[r + dr[k]][c + dc[k]];
                if (v < BLOCK_TYPE_BASE) continue; /* empty or STEEL */
                if (v == JOKER) continue;          /* jokers don't match each other */
                if (neighbour_colour == 0) {
                    neighbour_colour = v;
                } else if (v != neighbour_colour) {
                    consistent = 0;
                    break;
                }
            }
            if (!consistent || neighbour_colour == 0) continue;

            mark[r][c] = 1;
            for (k = 0; k < 4; k++) {
                int nr = r + dr[k];
                int nc = c + dc[k];
                if (grid[nr][nc] == neighbour_colour) mark[nr][nc] = 1;
            }
        }
    }

    for (r = 0; r < OUTER_H; r++) {
        for (c = 0; c < OUTER_W; c++) {
            if (mark[r][c]) {
                out[out_n].row = (unsigned short)r;
                out[out_n].col = (unsigned short)c;
                out_n++;
            }
        }
    }
    return out_n;
}

/* Ordinary matches always resolve first; only once none are left does
 * a joker get a chance to activate -- see find_joker_matches(). */
int find_matches(Grid grid, Cell *out) {
    int n = find_ordinary_matches(grid, out);
    if (n > 0) return n;
    return find_joker_matches(grid, out);
}

int settle(Grid grid, int *score) {
    int total_cleared = 0;
    int chain = 1;
    for (;;) {
        Cell to_clear[INNER_H * INNER_W];
        int n, i;

        apply_gravity(grid);
        n = find_matches(grid, to_clear);
        if (n == 0) break;

        for (i = 0; i < n; i++) grid[to_clear[i].row][to_clear[i].col] = 0;
        *score += n * 10 * chain;
        chain++;
        total_cleared += n;
    }
    return total_cleared;
}

int is_grid_empty(Grid grid) {
    int r, c;
    for (r = PLAY_MIN_ROW; r <= PLAY_MAX_ROW; r++) {
        for (c = PLAY_MIN_COL; c <= PLAY_MAX_COL; c++) {
            if (grid[r][c] != 0 && grid[r][c] != STEEL) return 0;
        }
    }
    return 1;
}

int has_any_move(Grid grid) {
    int r, c;
    for (r = PLAY_MIN_ROW; r <= PLAY_MAX_ROW; r++) {
        for (c = PLAY_MIN_COL; c <= PLAY_MAX_COL; c++) {
            unsigned char v = grid[r][c];
            if (v == 0 || v == STEEL) continue;
            if (c - 1 >= PLAY_MIN_COL && grid[r][c - 1] == 0) return 1;
            if (c + 1 <= PLAY_MAX_COL && grid[r][c + 1] == 0) return 1;
        }
    }
    return 0;
}

int has_orphan_block(Grid grid) {
    int counts[BLOCK_TYPE_BASE + STANDARD_BLOCK_COUNT];
    int r, c, i;
    int joker_count = 0;

    for (i = 0; i < BLOCK_TYPE_BASE + STANDARD_BLOCK_COUNT; i++) counts[i] = 0;

    for (r = PLAY_MIN_ROW; r <= PLAY_MAX_ROW; r++) {
        for (c = PLAY_MIN_COL; c <= PLAY_MAX_COL; c++) {
            unsigned char v = grid[r][c];
            if (v == JOKER) {
                joker_count++;
                continue;
            }
            /* JOKER isn't an ordinary colour (see game.h) -- counts[]
             * is sized for colour values only, indexing it with JOKER
             * (18) would be one past the end. */
            if (v == 0 || v == STEEL) continue;
            counts[v]++;
        }
    }
    /* A joker can rescue one lone-instance colour (that's its whole
     * purpose -- see game.h's JOKER comment), but only one: it clears
     * itself together with whichever single colour it ends up
     * touching, then it's gone. So the board is only truly stuck once
     * there are more orphaned colours than jokers left to rescue them
     * -- one joker cannot save two different lone colours at once. */
    {
        int orphan_colors = 0;
        for (i = BLOCK_TYPE_BASE; i < BLOCK_TYPE_BASE + STANDARD_BLOCK_COUNT; i++) {
            if (counts[i] == 1) orphan_colors++;
        }
        return orphan_colors > joker_count;
    }
}

void segment_bounds(Grid grid, int col, int row, int *top, int *bottom) {
    int t = row, b = row;
    while (t - 1 >= PLAY_MIN_ROW && grid[t - 1][col] != STEEL) t--;
    while (b + 1 <= PLAY_MAX_ROW && grid[b + 1][col] != STEEL) b++;
    *top = t;
    *bottom = b;
}

int move_block(Grid grid, int row, int col, int dc, int *target_col) {
    int tc = col + dc;
    unsigned char moving;

    if (tc < PLAY_MIN_COL || tc > PLAY_MAX_COL) return 0;
    moving = grid[row][col];
    if (moving == 0 || moving == STEEL) return 0;
    if (grid[row][tc] != 0) return 0;

    grid[row][tc] = moving;
    grid[row][col] = 0;
    *target_col = tc;
    return 1;
}

int cursor_landing_row(Grid grid, int target_col, int seg_row) {
    int top, bottom, r;
    segment_bounds(grid, target_col, seg_row, &top, &bottom);
    for (r = top; r <= bottom; r++) {
        if (grid[r][target_col] != 0) return r;
    }
    return bottom;
}

int compute_fallers(Grid grid, Faller *out) {
    unsigned char before_row[INNER_W][INNER_H];
    unsigned char before_type[INNER_W][INNER_H];
    int before_n[INNER_W];
    int c, r, out_n = 0;

    for (c = PLAY_MIN_COL; c <= PLAY_MAX_COL; c++) {
        int ci = c - PLAY_MIN_COL;
        int n = 0;
        for (r = PLAY_MIN_ROW; r <= PLAY_MAX_ROW; r++) {
            if (grid[r][c] != 0 && grid[r][c] != STEEL) {
                before_row[ci][n] = (unsigned char)r;
                before_type[ci][n] = grid[r][c];
                n++;
            }
        }
        before_n[ci] = n;
    }

    apply_gravity(grid);

    for (c = PLAY_MIN_COL; c <= PLAY_MAX_COL; c++) {
        int ci = c - PLAY_MIN_COL;
        int n = 0;
        for (r = PLAY_MIN_ROW; r <= PLAY_MAX_ROW; r++) {
            if (grid[r][c] != 0 && grid[r][c] != STEEL) {
                if (n < before_n[ci] && before_row[ci][n] != r) {
                    out[out_n].col = (unsigned char)c;
                    out[out_n].from_row = before_row[ci][n];
                    out[out_n].to_row = (unsigned char)r;
                    out[out_n].type = grid[r][c];
                    out_n++;
                }
                n++;
            }
        }
    }
    return out_n;
}

void counts_by_type(Grid grid, int *out) {
    int r, c, i;
    for (i = 0; i < STANDARD_BLOCK_COUNT; i++) out[i] = 0;
    for (r = PLAY_MIN_ROW; r <= PLAY_MAX_ROW; r++) {
        for (c = PLAY_MIN_COL; c <= PLAY_MAX_COL; c++) {
            unsigned char v = grid[r][c];
            if (v >= BLOCK_TYPE_BASE && v < BLOCK_TYPE_BASE + STANDARD_BLOCK_COUNT) {
                out[v - BLOCK_TYPE_BASE]++;
            }
        }
    }
}
