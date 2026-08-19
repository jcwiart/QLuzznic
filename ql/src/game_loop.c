/* game_loop.c -- the interactive game loop: title screen (real logo +
 * bounce-in intro, NEW GAME / ENTER CODE menu), the 100-level QLuzznic
 * campaign (src/levels.c, converted from qluzznic-levels.json -- see
 * [[qluzznic-own-levels-and-joker]]), and the interactive play loop
 * (arrows move the cursor, Space+Left/Right slides the block under it,
 * Escape restarts the current level), advancing/retrying once a level
 * ends (won, stuck, or timed out). Also hosts the one-time joker
 * explanation screen and the free-typed hex continue-code entry
 * screen (level_code.h).
 *
 * HUD: a 32px band across the top of the screen (freed up by the
 * border-row/column half-tile trick -- see render.h), the real logo
 * top-left, level/score/time as small-font text to its right (left-
 * aligned, no padding -- see draw_level_number/draw_time_number/
 * draw_score_number). No per-colour legend (dropped for space, not
 * essential). */

#include "game.h"
#include "render.h"
#include "animate.h"
#include "sound.h"
#include "text.h"
#include "level_code.h"

extern void wait_vbl(void);
extern void fill_screen(unsigned char even_byte, unsigned char odd_byte);
extern int kbd_row(int row);
extern void clear_rect(int x, int y, int w, int h);
extern void blit_tile(int col, int row, int sprite_index);
extern void blit_logo_mini(void);
extern void decode_logo_big(void);
extern void ripple_logo_advance(unsigned char *y_history, int new_head_y);

#define KEY_ROW1 1
#define K1_LEFT  1
#define K1_UP    2
#define K1_ESC   3
#define K1_RIGHT 4
#define K1_SPACE 6
#define K1_DOWN  7

/* Dev shortcut: Shift+Tab skips to the next level. Row/bit found from
 * sQLux's KEYROW emulation (SDL2screen.c: row=7-code/8, col=1<<(code%8)):
 * QL_TAB=0x13 -> row 5 bit 3; Shift isn't a normal matrix key, it's
 * added into row 7 bit 0 on top of whatever real keys are there. */
#define KEY_ROW_TAB 5
#define K_TAB 3
#define KEY_ROW_SHIFT 7
#define K_SHIFT 0

/* ESDF and IJKL as left-hand/right-hand alternates to the arrow keys:
 * on a real QL the arrows sit right next to the space bar (see
 * K1_LEFT/K1_RIGHT/K1_SPACE above, all in KEY_ROW1), cramped for
 * actual play. Row/bit for each found the same way as HEX_KEY_TABLE
 * below (sQLux's qlkeys.h QL_* codes through row=7-code/8,
 * bit=code%8); QL_D/E/F cross-checked exactly against that table's
 * existing D/E/F entries before trusting the formula for the other,
 * previously-unused keys (S, I, J, K, L). Conveniently, S/F share row
 * 3 with K, and D shares row 4 with J/L, so only 4 kbd_row reads
 * (rows 3, 4, 5, 6) cover both schemes. add_alt_movement_bits ORs all
 * of them into the same K1_UP/LEFT/DOWN/RIGHT bit positions KEY_ROW1
 * already uses, so every caller of kbd_row(KEY_ROW1) downstream (menu
 * selection, cursor movement, the space+arrow slide) sees ESDF, IJKL
 * and the arrows as fully interchangeable without any other code
 * change. */
#define ESDF_ROW_SF 3
#define ESDF_BIT_S 3
#define ESDF_BIT_F 4
#define ESDF_ROW_D 4
#define ESDF_BIT_D 6
#define ESDF_ROW_E 6
#define ESDF_BIT_E 4

#define IJKL_ROW_K 3  /* same row as ESDF_ROW_SF */
#define IJKL_BIT_K 2
#define IJKL_ROW_JL 4 /* same row as ESDF_ROW_D */
#define IJKL_BIT_J 7
#define IJKL_BIT_L 0
#define IJKL_ROW_I 5
#define IJKL_BIT_I 2

static int add_alt_movement_bits(int bits) {
    int row3 = kbd_row(ESDF_ROW_SF); /* S, F, K */
    int row4 = kbd_row(ESDF_ROW_D);  /* D, J, L */

    if (row3 & (1 << ESDF_BIT_S)) bits |= (1 << K1_LEFT);
    if (row3 & (1 << ESDF_BIT_F)) bits |= (1 << K1_RIGHT);
    if (row3 & (1 << IJKL_BIT_K)) bits |= (1 << K1_DOWN);

    if (row4 & (1 << ESDF_BIT_D)) bits |= (1 << K1_DOWN);
    if (row4 & (1 << IJKL_BIT_J)) bits |= (1 << K1_LEFT);
    if (row4 & (1 << IJKL_BIT_L)) bits |= (1 << K1_RIGHT);

    if (kbd_row(ESDF_ROW_E) & (1 << ESDF_BIT_E)) bits |= (1 << K1_UP);
    if (kbd_row(IJKL_ROW_I) & (1 << IJKL_BIT_I)) bits |= (1 << K1_UP);

    return bits;
}

/* game.js: CURSOR_BLINK_MS=120 per animation frame. At 50Hz, ~6 VBL
 * frames per step. */
#define CURSOR_FRAME_TICKS 6

/* 50Hz PAL frame rate -- one real second of countdown per 50 VBLs. */
#define FRAMES_PER_SECOND 50

/* HUD band, y=0..31 (freed up by the border-row/column half-tile
 * trick -- see render.h's HUD_HEIGHT). Real logo top-left (100x29,
 * blit_logo_mini in asm/display.s -- see tools/png2logo.py), label+
 * number fields squeezed to its right. Numbers are left-aligned, no
 * padding (draw_number in text.c) -- each field's X is a fixed
 * constant sized for the common case (2-digit level, 3-digit time),
 * not recomputed from the actual digit count, so a rarer wider value
 * (level 100, a 3-digit level number) just leaves a smaller-but-still-
 * positive gap before the next field instead of overlapping it. Number
 * fields only (not labels) get cleared+redrawn on updates, so the
 * static labels only need drawing once (draw_hud_static, called after
 * fill_screen). All x values are multiples of 4 (mode 8's group-
 * alignment rule). */
#define LEVEL_LABEL_X 108 /* logo's 100px width + 8px gap */
#define LEVEL_LABEL_Y 4
#define LEVEL_NUMBER_X 156 /* LEVEL_LABEL_X + 5*GLYPH_PX("LEVEL") + 8px gap */
#define LEVEL_NUMBER_Y 4
#define LEVEL_CLEAR_X LEVEL_NUMBER_X
#define LEVEL_CLEAR_Y LEVEL_NUMBER_Y
#define LEVEL_CLEAR_W 24 /* 3 digits reserved (level can reach 100) */
#define LEVEL_CLEAR_H GLYPH_PX

#define TIME_LABEL_X 188 /* LEVEL_NUMBER_X + 2*GLYPH_PX(2-digit level) + 16px gap */
#define TIME_LABEL_Y 4
#define TIME_NUMBER_X 228 /* TIME_LABEL_X + 4*GLYPH_PX("TIME") + 8px gap */
#define TIME_NUMBER_Y 4
#define TIME_CLEAR_X TIME_NUMBER_X
#define TIME_CLEAR_Y TIME_NUMBER_Y
#define TIME_CLEAR_W 24 /* 3 digits (time is whole seconds now, max LEVEL_TIME_SECONDS=120) -- reaches x=252, 4px shy of the 256px screen edge */
#define TIME_CLEAR_H GLYPH_PX

#define SCORE_LABEL_X 108 /* under LEVEL_LABEL_X, past the logo's 100px width */
#define SCORE_LABEL_Y 18
#define SCORE_NUMBER_X 156 /* SCORE_LABEL_X + 5*GLYPH_PX("SCORE") + 8px gap */
#define SCORE_NUMBER_Y 18
#define SCORE_CLEAR_X SCORE_NUMBER_X
#define SCORE_CLEAR_Y SCORE_NUMBER_Y
#define SCORE_CLEAR_W 56 /* generous headroom: up to 6 digits + a "." group separator */
#define SCORE_CLEAR_H GLYPH_PX

/* End-of-level banners, centred over the board (MARGIN_X 8..248, full
 * screen height 0..256 now that tiles are 24x32). "LEVEL"/"COMPLETE"
 * need two lines (80px/128px wide); "GAME OVER" fits on one line
 * (9*16=144px). All centres come out a multiple of 4 already. */
#define BANNER_LINE1_X 88
#define BANNER_LINE1_Y 112
#define BANNER_LINE2_X 64
#define BANNER_LINE2_Y 128
#define GAMEOVER_X 56
#define GAMEOVER_Y 120
#define BANNER_BLINK_TICKS 25 /* 0.5s at 50Hz */

/* Small-font hint line under each banner, centred the same way (board
 * centre x=128): "PRESS SPACE TO RETRY" is 21*8=168px wide, "...TO
 * CONTINUE" is 24*8=192px -- both centres are already multiples of 4. */
#define COMPLETE_HINT_X 32
#define COMPLETE_HINT_Y 152
#define GAMEOVER_HINT_X 44
#define GAMEOVER_HINT_Y 144

/* Continue code shown under the LEVEL COMPLETE banner (see
 * level_code.h) -- "CODE:" (5*8=40px) + 4 hex digits (4*8=32px) =
 * 72px, centred: X=(256-72)/2=92. */
#define CODE_LEN 4
#define CODE_LABEL_X 92
#define CODE_LABEL_Y 168
#define CODE_DIGITS_X 132 /* CODE_LABEL_X + 5*GLYPH_PX ("CODE:" incl. no trailing space) */

/* Title screen, shown once before the first level loads. Real logo
 * (200x58, ripple_logo_advance in asm/display.s -- see
 * tools/png2logo.py) replaces the old placeholder banner-font
 * "QLUZZNIC" text; x is fixed at compile time in the asm
 * (logo_big_x=28 there), y is a runtime argument so the logo can be
 * redrawn at a different height each frame for the continuously
 * looping, rippling bounce below. Control summary lines shifted down
 * 8px to clear the logo's greater height (58px vs the old text's
 * 16px). */
#define TITLE_LOGO_X 28       /* must match asm/display.s's logo_big_x equ exactly */
#define TITLE_LOGO_W 200      /* must match asm/display.s's logo_big_w_groups*4 exactly */
#define TITLE_LOGO_H 58       /* must match asm/display.s's logo_big_h exactly */
#define TITLE_LOGO_REST_Y 24  /* final resting Y, matches the old fixed blit_logo_big's position */
#define TITLE_LOGO_CLEAR_Y 12 /* top of the (now half-height) bounce range -- must match TITLE_LOGO_BOUNCE_Y[0] */
#define TITLE_LOGO_CLEAR_H (TITLE_LOGO_REST_Y + TITLE_LOGO_H) /* bounce range + logo height, covers every frame's start position -- cleared before the first bounce and again before each replay */

#define LOGO_RIPPLE_COLUMNS 25 /* must match asm/display.s's logo_big_row_bytes/logo_ripple_col_bytes exactly (8px = 4 bytes per column) */

/* Perpetual bounce curve for the title logo: falls from
 * TITLE_LOGO_CLEAR_Y accelerating under gravity (g=0.45px/frame^2,
 * simulated in Python with float precision then rounded to whole
 * pixels, landing exactly on TITLE_LOGO_REST_Y, the "floor" -- a
 * 12px amplitude, half of an earlier 24px/full-height version),
 * then rises back up decelerating along the exact mirror of the
 * fall (the same rounded deltas, reversed and negated, so the rise
 * is a perfect visual mirror of the fall rather than a separately-
 * rounded and possibly lopsided curve) -- a momentary 1-frame hover
 * at the top (TITLE_LOGO_CLEAR_Y shows twice back to back, once
 * ending a rise and once starting the next fall) is the natural apex
 * of a real parabolic bounce, not a glitch. This g keeps the max
 * single-frame delta to 3px over 14 total frames -- a first, steeper
 * g=1.0 curve reached the floor in far fewer frames but with much
 * bigger per-frame deltas, a visible "jump" right around impact even
 * though the timing itself was smooth. No restitution/decay: unlike
 * a real dropped ball, this loops the *same* fall-rise cycle forever,
 * so it never settles -- see show_title_screen below, which just
 * walks this table on a repeating index for as long as the title
 * screen is up. Precomputed as plain Y positions (no runtime physics/
 * float math at all -- the QL only ever walks this table frame by
 * frame).
 *
 * Redrawing the logo used to mean re-decoding its PackBits stream on
 * every single frame (see packbits_next_pair in asm/display.s), too
 * slow to do smoothly every 50Hz tick on a 68008 -- a first, denser
 * 45-step attempt at this same curve made it noticeably slow with
 * visible flicker (the clear-then-redraw gap became visible on
 * screen) because each "frame" was actually taking multiple real VBL
 * periods to draw. Only clearing the exposed sliver each step (not
 * the whole bounding box), plus decode_logo_big/ripple_logo_advance
 * (decode the PackBits stream once into logo_big_cache, then every
 * frame is a plain word copy) fixed the slowness, which is what makes
 * looping this forever affordable. */
static const unsigned char TITLE_LOGO_BOUNCE_Y[] = {
    12, 12, 13, 15, 16, 19, 21, 24, 21, 19, 16, 15, 13, 12,
};
#define TITLE_LOGO_BOUNCE_FRAMES (sizeof(TITLE_LOGO_BOUNCE_Y) / sizeof(TITLE_LOGO_BOUNCE_Y[0]))

#define TITLE_LINE1_X 56  /* "ARROWS MOVE CURSOR" = 18*8=144px */
#define TITLE_LINE1_Y 96
#define TITLE_LINE2_X 28  /* "SPACE THEN ARROW TO SLIDE" = 25*8=200px */
#define TITLE_LINE2_Y 112
#define TITLE_LINE3_X 44  /* "ESCAPE RESTARTS LEVEL" = 21*8=168px */
#define TITLE_LINE3_Y 128
#define TITLE_LINE4_X 12  /* "ESDF OR IJKL ALSO MOVE CURSOR" = 29*8=232px */
#define TITLE_LINE4_Y 144

/* Title screen menu: NEW GAME / ENTER CODE, Up/Down to select (only
 * two items, so either arrow just flips the choice), Space to
 * confirm. Sits below the existing control summary, still well
 * inside the 256px screen height. Items share a left edge rather
 * than each being individually centred (roughly centres the wider
 * "ENTER CODE", 10*8=80px -> X=(256-80)/2=88); the pointer glyph
 * sits 16px to the left of that. */
#define MENU_ITEM_X 88
#define MENU_POINTER_X 72
#define MENU_ITEM1_Y 180  /* NEW GAME */
#define MENU_ITEM2_Y 196  /* ENTER CODE */
#define MENU_HINT_X 36    /* "SELECT THEN PRESS SPACE" = 23*8=184px */
#define MENU_HINT_Y 220

/* Enter-code screen: a dedicated full-screen (fill_screen'd before
 * and after by the caller) letting the player type a 4-character hex
 * code from the keyboard. All X values centre their line and are
 * already multiples of 4. */
#define ENTER_CODE_TITLE_X 88   /* "ENTER CODE" = 10*8=80px */
#define ENTER_CODE_TITLE_Y 40
#define ENTER_CODE_HINT_X 28    /* "TYPE THE 4 CHARACTER CODE" = 25*8=200px */
#define ENTER_CODE_HINT_Y 72
#define ENTER_CODE_ESC_X 44     /* "PRESS ESCAPE FOR MENU" = 21*8=168px */
#define ENTER_CODE_ESC_Y 96
#define ENTER_CODE_INPUT_LABEL_X 92 /* "CODE:" (40px) + 4 hex digits (32px), centred */
#define ENTER_CODE_INPUT_Y 140
#define ENTER_CODE_INPUT_CHARS_X 132 /* ENTER_CODE_INPUT_LABEL_X + 5*GLYPH_PX */
#define ENTER_CODE_ERROR_X 80   /* "INVALID CODE" = 12*8=96px */
#define ENTER_CODE_ERROR_Y 170
#define ENTER_CODE_ERROR_W 96

/* Joker tip screen, shown once the first time the player reaches
 * FIRST_JOKER_LEVEL (game.h/levels.c). Tile centred (col 5 -> pixel x
 * = 5*24-4 = 116, tile width 24 -> right edge 140, centred on the
 * 256px screen), text lines below it, same centring approach as the
 * title screen (each line's X is (256 - chars*8)/2, rounded to a
 * multiple of 4). */
#define JOKER_TIP_TILE_COL 5
#define JOKER_TIP_TILE_ROW 1
#define JOKER_TIP_LINE1_X 36  /* "THE JOKER BLOCK IS WILD" = 23*8=184px */
#define JOKER_TIP_LINE1_Y 96
#define JOKER_TIP_LINE2_X 24  /* "IT VANISHES WITH ONE COLOR" = 26*8=208px */
#define JOKER_TIP_LINE2_Y 112
#define JOKER_TIP_LINE3_X 32  /* "TWO COLORS: IT STAYS PUT" = 24*8=192px */
#define JOKER_TIP_LINE3_Y 128
#define JOKER_TIP_HINT_X 36   /* "PRESS SPACE TO CONTINUE" = 23*8=184px */
#define JOKER_TIP_HINT_Y 160

typedef enum { BANNER_NONE, BANNER_LEVEL_COMPLETE, BANNER_GAME_OVER } BannerKind;

static Grid grid;
static int level_index = 0;
static int score;
static int time_left;
static int cur_row, cur_col;
static int cursor_frame = 0;
static int game_over;
static BannerKind banner_kind;
static int banner_visible;
static int banner_ticks;
static int joker_tip_shown = 0;
static int level_start_score = 0;

/* Draws the HUD's static parts (logo + field labels) --
 * called once, right after fill_screen. The number fields drawn over
 * them change often (every level load, every second, every match) and
 * are each redrawn independently below without touching the labels. */
static void draw_hud_static(void) {
    blit_logo_mini();
    draw_string(LEVEL_LABEL_X, LEVEL_LABEL_Y, "LEVEL");
    draw_string(TIME_LABEL_X, TIME_LABEL_Y, "TIME");
    draw_string(SCORE_LABEL_X, SCORE_LABEL_Y, "SCORE");
}

static void draw_level_number(void) {
    clear_rect(LEVEL_CLEAR_X, LEVEL_CLEAR_Y, LEVEL_CLEAR_W, LEVEL_CLEAR_H);
    draw_number(LEVEL_NUMBER_X, LEVEL_NUMBER_Y, level_index + 1); /* level_index is 0-based (LEVELS[]); display is 1-based */
}

/* Formats value with a "." every 3 digits from the right (French
 * thousands grouping, e.g. 6500 -> "6.500") into out, which the
 * caller must size generously (SCORE_CLEAR_W's comment) -- score only
 * ever grows within a level, and score/1000-style division here
 * reuses the same runtime division routine load_level()'s `%
 * LEVEL_COUNT` already pulls in, not a new one (see
 * [[ql-boot-crash-is-memory]]). */
static void format_grouped_score(char *out, int value) {
    char digits[12];
    int n = 0;
    int v = value;
    int i, o = 0;

    if (v <= 0) {
        digits[n++] = '0';
    } else {
        while (v > 0) {
            digits[n++] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
    for (i = 0; i < n; i++) {
        int remaining_after = (n - i) - 1;
        out[o++] = digits[n - 1 - i];
        if (remaining_after > 0 && remaining_after % 3 == 0) {
            out[o++] = '.';
        }
    }
    out[o] = '\0';
}

static void draw_score_number(void) {
    char buf[16];
    clear_rect(SCORE_CLEAR_X, SCORE_CLEAR_Y, SCORE_CLEAR_W, SCORE_CLEAR_H);
    format_grouped_score(buf, score);
    draw_string(SCORE_NUMBER_X, SCORE_NUMBER_Y, buf);
}

static void draw_time_number(void) {
    clear_rect(TIME_CLEAR_X, TIME_CLEAR_Y, TIME_CLEAR_W, TIME_CLEAR_H);
    draw_number(TIME_NUMBER_X, TIME_NUMBER_Y, time_left);
}

/* Draws the continue code for the level after this one (see
 * level_code.h) -- called from show_banner() so it blinks along with
 * the rest of the LEVEL COMPLETE banner and gets erased the same way
 * by hide_banner()'s draw_board(). */
static void draw_level_code(void) {
    unsigned char bytes[2];
    int next_level = (level_index + 1) % LEVEL_COUNT;

    encode_level_code(next_level, bytes);
    draw_string(CODE_LABEL_X, CODE_LABEL_Y, "CODE:");
    draw_char(CODE_DIGITS_X + 0 * GLYPH_PX, CODE_LABEL_Y, hex_digit_char(bytes[0] >> 4));
    draw_char(CODE_DIGITS_X + 1 * GLYPH_PX, CODE_LABEL_Y, hex_digit_char(bytes[0] & 0xF));
    draw_char(CODE_DIGITS_X + 2 * GLYPH_PX, CODE_LABEL_Y, hex_digit_char(bytes[1] >> 4));
    draw_char(CODE_DIGITS_X + 3 * GLYPH_PX, CODE_LABEL_Y, hex_digit_char(bytes[1] & 0xF));
}

static void show_banner(void) {
    if (banner_kind == BANNER_LEVEL_COMPLETE) {
        draw_banner_string(BANNER_LINE1_X, BANNER_LINE1_Y, "LEVEL");
        draw_banner_string(BANNER_LINE2_X, BANNER_LINE2_Y, "COMPLETE");
        draw_string(COMPLETE_HINT_X, COMPLETE_HINT_Y, "PRESS SPACE TO CONTINUE");
        draw_level_code();
    } else if (banner_kind == BANNER_GAME_OVER) {
        draw_banner_string(GAMEOVER_X, GAMEOVER_Y, "GAME OVER");
        draw_string(GAMEOVER_HINT_X, GAMEOVER_HINT_Y, "PRESS SPACE TO RETRY");
    }
}

static void trigger_game_over(BannerKind kind) {
    game_over = 1;
    banner_kind = kind;
    show_banner();
    banner_visible = 1;
    banner_ticks = 0;
    if (kind == BANNER_LEVEL_COMPLETE) {
        play_win_jingle();
    } else {
        play_lose_jingle();
    }
}

/* Hides the banner by redrawing the board it overlays. The banner's
 * pixel positions don't land on grid tile boundaries, so a per-cell
 * erase can't target just the covered area -- this only runs a couple
 * of times a second while the board is otherwise static (frozen at
 * game_over), so a full redraw is cheap enough. */
static void hide_banner(void) {
    draw_board(grid);
}

/* Blocks until Space is pressed (edge-triggered). Shared by the title
 * screen and the joker tip screen -- both are full-screen "read this,
 * then press Space" interludes outside the normal game_loop() input
 * handling. */
static void wait_for_space(void) {
    int bits, prev_bits = 0;

    for (;;) {
        wait_vbl();
        bits = kbd_row(KEY_ROW1);
        if ((bits & ~prev_bits) & (1 << K1_SPACE)) break;
        prev_bits = bits;
    }
}

typedef enum { MENU_NEW_GAME, MENU_ENTER_CODE } TitleMenuChoice;

static void draw_menu_pointer(TitleMenuChoice choice) {
    draw_char(MENU_POINTER_X, MENU_ITEM1_Y, (choice == MENU_NEW_GAME) ? '>' : ' ');
    draw_char(MENU_POINTER_X, MENU_ITEM2_Y, (choice == MENU_ENTER_CODE) ? '>' : ' ');
}

/* Shows the title screen's NEW GAME / ENTER CODE menu and blocks
 * until Space confirms a choice -- Up or Down just flips the
 * selection since there are only two items. Called once, before the
 * first level loads. */
static TitleMenuChoice show_title_screen(void) {
    TitleMenuChoice choice = MENU_NEW_GAME;
    int bits, pressed, prev_bits = 0;
    int logo_frame = 0;
    unsigned char logo_col_history[LOGO_RIPPLE_COLUMNS]; /* per-column Y, one VBL further behind per column -- see the ripple loop below */
    int col_i;
    for (col_i = 0; col_i < LOGO_RIPPLE_COLUMNS; col_i++) {
        logo_col_history[col_i] = TITLE_LOGO_BOUNCE_Y[0];
    }

    /* decode the logo's PackBits stream once instead of on every frame
     * of every bounce cycle -- see TITLE_LOGO_BOUNCE_Y's comment. */
    decode_logo_big();
    clear_rect(TITLE_LOGO_X, TITLE_LOGO_CLEAR_Y, TITLE_LOGO_W, TITLE_LOGO_CLEAR_H);

    draw_string(TITLE_LINE1_X, TITLE_LINE1_Y, "ARROWS MOVE CURSOR");
    draw_string(TITLE_LINE2_X, TITLE_LINE2_Y, "SPACE THEN ARROW TO SLIDE");
    draw_string(TITLE_LINE3_X, TITLE_LINE3_Y, "ESCAPE RESTARTS LEVEL");
    draw_string(TITLE_LINE4_X, TITLE_LINE4_Y, "ESDF OR IJKL ALSO MOVE CURSOR");
    draw_string(MENU_ITEM_X, MENU_ITEM1_Y, "NEW GAME");
    draw_string(MENU_ITEM_X, MENU_ITEM2_Y, "ENTER CODE");
    draw_string(MENU_HINT_X, MENU_HINT_Y, "SELECT THEN PRESS SPACE");
    draw_menu_pointer(choice);

    /* The logo keeps rippling for as long as the title screen is up --
     * one step of TITLE_LOGO_BOUNCE_Y per tick, wrapping back to frame
     * 0 forever, interleaved with the existing key poll below so a
     * selection can land on any frame. */
    for (;;) {
        int y;

        wait_vbl();

        y = TITLE_LOGO_BOUNCE_Y[logo_frame];

        /* Shifts the per-column history (column c takes column c-1's
         * old Y, one extra VBL of delay per column to the right, which
         * is what gives the wave its "sweeping across" look), clears
         * only each column's own exposed sliver, and redraws every
         * column from logo_big_cache -- all folded into one asm call
         * now; see ripple_logo_advance's comment in asm/display.s for
         * why this used to be a C-side loop calling clear_rect per
         * column instead. */
        ripple_logo_advance(logo_col_history, y);

        logo_frame++;
        if (logo_frame == (int)TITLE_LOGO_BOUNCE_FRAMES) {
            logo_frame = 0;
        }

        bits = add_alt_movement_bits(kbd_row(KEY_ROW1));
        pressed = bits & ~prev_bits;
        prev_bits = bits;

        if ((pressed & (1 << K1_UP)) || (pressed & (1 << K1_DOWN))) {
            choice = (choice == MENU_NEW_GAME) ? MENU_ENTER_CODE : MENU_NEW_GAME;
            draw_menu_pointer(choice);
        }
        if (pressed & (1 << K1_SPACE)) break;
    }
    return choice;
}

/* Shows a one-time explanation of the joker piece (art + rules) and
 * blocks until Space is pressed -- called from load_level() the first
 * time the player reaches FIRST_JOKER_LEVEL, before that level's board
 * is drawn. Caller is responsible for clearing the screen before and
 * restoring the HUD after (see load_level()). */
static void show_joker_tip(void) {
    blit_tile(JOKER_TIP_TILE_COL, JOKER_TIP_TILE_ROW, JOKER_SPRITE_INDEX);
    draw_string(JOKER_TIP_LINE1_X, JOKER_TIP_LINE1_Y, "THE JOKER BLOCK IS WILD");
    draw_string(JOKER_TIP_LINE2_X, JOKER_TIP_LINE2_Y, "IT VANISHES WITH ONE COLOR");
    draw_string(JOKER_TIP_LINE3_X, JOKER_TIP_LINE3_Y, "TWO COLORS: IT STAYS PUT");
    draw_string(JOKER_TIP_HINT_X, JOKER_TIP_HINT_Y, "PRESS SPACE TO CONTINUE");
    wait_for_space();
}

/* Row/bit position of each hex-entry key on the QL's 8x8 keyboard
 * matrix, derived from sQLux's include/qlkeys.h QL_* codes via the
 * same row=7-code/8, bit=code%8 formula already used for the
 * Shift+Tab dev shortcut above (verified against every key already
 * used in this project -- arrows/space/enter/esc/tab all land exactly
 * where keyboard.s/game_loop.c already say they do). Nothing in this
 * project has read letter keys before, so this table only covers the
 * 16 keys the code-entry screen actually needs. */
typedef struct {
    unsigned char row;
    unsigned char bit;
    char ch;
} HexKeyPos;

static const HexKeyPos HEX_KEY_TABLE[16] = {
    {6, 5, '0'}, {4, 3, '1'}, {6, 1, '2'}, {4, 1, '3'},
    {0, 6, '4'}, {0, 2, '5'}, {6, 2, '6'}, {0, 7, '7'},
    {6, 0, '8'}, {5, 0, '9'}, {4, 4, 'A'}, {2, 4, 'B'},
    {2, 3, 'C'}, {4, 6, 'D'}, {6, 4, 'E'}, {3, 4, 'F'},
};

/* Polls the 6 keyboard rows the hex keys live on (rows 0,2,3,4,5,6 --
 * row 1 is read separately by the caller for Escape) and returns the
 * hex character of whichever key was freshly pressed this frame, or 0
 * if none. `prev_row_bits` is a 7-entry (indices 0-6) caller-owned
 * edge-detection array, same edge-triggered pattern as the rest of
 * this file's input handling. */
static char poll_hex_key(int *prev_row_bits) {
    static const unsigned char ROWS[6] = {0, 2, 3, 4, 5, 6};
    int bits[7];
    int i, r;
    char result = 0;

    for (i = 0; i < 6; i++) {
        r = ROWS[i];
        bits[r] = kbd_row(r);
    }

    for (i = 0; i < 16; i++) {
        int row = HEX_KEY_TABLE[i].row;
        if ((bits[row] & ~prev_row_bits[row]) & (1 << HEX_KEY_TABLE[i].bit)) {
            result = HEX_KEY_TABLE[i].ch;
        }
    }

    for (i = 0; i < 6; i++) {
        r = ROWS[i];
        prev_row_bits[r] = bits[r];
    }
    return result;
}

/* Lets the player type a 4-character hex code (level_code.h) to jump
 * straight to a level -- the ENTER CODE menu option. Returns 1 and
 * sets *out_level_index on a valid code; returns 0 (unchanged) if
 * Escape is pressed to back out to the title menu. Caller clears the
 * screen before and after, same convention as show_joker_tip(). */
static int show_enter_code_screen(int *out_level_index) {
    int prev_row_bits[7] = {0, 0, 0, 0, 0, 0, 0};
    int prev_esc_bits = 0;
    char typed[CODE_LEN];
    int n = 0;

    draw_string(ENTER_CODE_TITLE_X, ENTER_CODE_TITLE_Y, "ENTER CODE");
    draw_string(ENTER_CODE_HINT_X, ENTER_CODE_HINT_Y, "TYPE THE 4 CHARACTER CODE");
    draw_string(ENTER_CODE_ESC_X, ENTER_CODE_ESC_Y, "PRESS ESCAPE FOR MENU");
    draw_string(ENTER_CODE_INPUT_LABEL_X, ENTER_CODE_INPUT_Y, "CODE:");

    for (;;) {
        char key;
        int esc_bits;

        wait_vbl();
        key = poll_hex_key(prev_row_bits);

        esc_bits = kbd_row(KEY_ROW1);
        if ((esc_bits & ~prev_esc_bits) & (1 << K1_ESC)) return 0;
        prev_esc_bits = esc_bits;

        if (key == 0) continue;

        typed[n] = key;
        draw_char(ENTER_CODE_INPUT_CHARS_X + n * GLYPH_PX, ENTER_CODE_INPUT_Y, key);
        n++;

        if (n == CODE_LEN) {
            unsigned char bytes[2];
            int level;

            bytes[0] = (unsigned char)((hex_digit_value(typed[0]) << 4) | hex_digit_value(typed[1]));
            bytes[1] = (unsigned char)((hex_digit_value(typed[2]) << 4) | hex_digit_value(typed[3]));

            if (decode_level_code(bytes, &level) && level >= 0 && level < LEVEL_COUNT) {
                *out_level_index = level;
                return 1;
            }

            draw_string(ENTER_CODE_ERROR_X, ENTER_CODE_ERROR_Y, "INVALID CODE");
            {
                int f;
                for (f = 0; f < FRAMES_PER_SECOND; f++) wait_vbl(); /* let them read it */
            }
            clear_rect(ENTER_CODE_ERROR_X, ENTER_CODE_ERROR_Y, ENTER_CODE_ERROR_W, GLYPH_PX);
            clear_rect(ENTER_CODE_INPUT_CHARS_X, ENTER_CODE_INPUT_Y, CODE_LEN * GLYPH_PX, GLYPH_PX);
            n = 0;
        }
    }
}

/* Loads level `idx` (wrapped into range) and draws everything fresh --
 * matches game.js's loadLevel(): timer reset, silent instant settle
 * (no animation) before the level becomes playable. Score persists
 * across levels (it's a running total for the whole play session) --
 * reset_score is only true for the very first level of a session
 * (fresh "NEW GAME" or a just-entered continue code), never for
 * advancing/retrying within one.
 *
 * level_start_score snapshots the score right as this level becomes
 * playable, BEFORE settle()'s own deterministic bonus (see below) --
 * restarting this same level (Escape, or a stuck-retry) restores score
 * to this value first so a replayed level's settle() bonus is applied
 * exactly once, matching a first-try clear. Without this, points from
 * an abandoned attempt (partial matches, or repeated settle bonuses)
 * would leak in, making a level worth more the more times it's
 * restarted -- see game_loop()'s Escape/retry handling. */
static void load_level(int idx, int reset_score) {
    level_index = ((idx % LEVEL_COUNT) + LEVEL_COUNT) % LEVEL_COUNT;

    if (level_index == FIRST_JOKER_LEVEL && !joker_tip_shown) {
        fill_screen(0, 0);
        show_joker_tip();
        fill_screen(0, 0);
        draw_hud_static();
        joker_tip_shown = 1;
    }

    build_level(&LEVELS[level_index], grid);
    if (reset_score) score = 0;
    level_start_score = score;
    time_left = LEVEL_TIME_SECONDS;
    settle(grid, &score);

    cur_row = PLAY_MIN_ROW;
    cur_col = PLAY_MIN_COL;
    game_over = 0;
    banner_kind = BANNER_NONE;
    banner_visible = 0;
    banner_ticks = 0;

    draw_level_number();
    draw_score_number();
    draw_time_number();
    draw_board(grid);
    draw_cursor(cur_row, cur_col, cursor_frame);
}

/* Never returns (see main.c/takeover's comment) -- the outer for(;;)
 * is a session loop: Shift+Escape below drops back to the title
 * screen instead of exiting, so a full trip through title -> play ->
 * title again is just another lap of this same loop, not a return. */
void game_loop(void) {
    for (;;) {
        int prev_bits = 0;
        int prev_tab_bits = 0;
        int cursor_ticks = 0;
        int second_ticks = 0;
        int start_level = 0;
        int chosen = 0;
        int return_to_title = 0;

        /* Screen memory still holds whatever SuperBASIC's mode 4 console
         * left there before takeover (or the previous lap's board) --
         * mode 8 reinterprets those bytes as pixels (including stray
         * flash bits), which is the striping/flicker seen without this
         * clear. */
        fill_screen(0, 0);
        while (!chosen) {
            TitleMenuChoice choice = show_title_screen();
            if (choice == MENU_NEW_GAME) {
                start_level = 0;
                chosen = 1;
            } else {
                fill_screen(0, 0);
                if (show_enter_code_screen(&start_level)) {
                    chosen = 1;
                } else {
                    fill_screen(0, 0); /* Escape: back to the title menu */
                }
            }
        }
        fill_screen(0, 0);
        draw_hud_static();
        load_level(start_level, 1); /* fresh session start: reset score */

        while (!return_to_title) {
            int bits, pressed, new_row, new_col, target_col;

            wait_vbl();

            bits = add_alt_movement_bits(kbd_row(KEY_ROW1));
            pressed = bits & ~prev_bits; /* edge-triggered: only just-pressed keys */
            prev_bits = bits;

            {
                /* dev shortcut: Shift+Tab skips to the next level, in play or frozen */
                int tab_bits = kbd_row(KEY_ROW_TAB);
                int tab_pressed = tab_bits & ~prev_tab_bits;
                int shift_bits = kbd_row(KEY_ROW_SHIFT);
                prev_tab_bits = tab_bits;

                if ((tab_pressed & (1 << K_TAB)) && (shift_bits & (1 << K_SHIFT))) {
                    load_level(level_index + 1, 0);
                    continue;
                }
                /* Shift+Escape: bail out to the title screen from
                 * anywhere, play or frozen -- same early placement as
                 * the Shift+Tab shortcut above so it isn't shadowed by
                 * the plain-Escape restart or the game_over block
                 * below. */
                if ((pressed & (1 << K1_ESC)) && (shift_bits & (1 << K_SHIFT))) {
                    return_to_title = 1;
                    continue;
                }
            }

            if (!game_over && (pressed & (1 << K1_ESC))) {
                /* restart the current level on demand -- rolls score back
                 * to level_start_score first so a restarted level is worth
                 * exactly what it was on a first try, never more (see
                 * load_level()'s comment). */
                score = level_start_score;
                load_level(level_index, 0);
                continue;
            }

            if (game_over) {
                if (pressed & (1 << K1_SPACE)) {
                    /* matches the "PRESS SPACE TO..." hint: retry the same
                     * (unsolved) level on a stuck/timeout game over, advance
                     * to the next one only after actually completing it --
                     * same distinction as game.js. */
                    if (banner_kind == BANNER_LEVEL_COMPLETE) {
                        load_level(level_index + 1, 0);
                    } else {
                        score = level_start_score;
                        load_level(level_index, 0);
                    }
                    continue;
                }
                if (banner_kind != BANNER_NONE) {
                    banner_ticks++;
                    if (banner_ticks >= BANNER_BLINK_TICKS) {
                        banner_ticks = 0;
                        banner_visible = !banner_visible;
                        if (banner_visible) show_banner(); else hide_banner();
                    }
                }
                continue;
            }

            cursor_ticks++;
            if (cursor_ticks >= CURSOR_FRAME_TICKS) {
                cursor_ticks = 0;
                cursor_frame = (cursor_frame + 1) % CURSOR_FRAME_COUNT;
                draw_cursor(cur_row, cur_col, cursor_frame);
            }

            second_ticks++;
            if (second_ticks >= FRAMES_PER_SECOND) {
                second_ticks = 0;
                time_left--;
                if (time_left <= 0) {
                    time_left = 0;
                    draw_time_number();
                    trigger_game_over(BANNER_GAME_OVER); /* time's up */
                    continue;
                }
                draw_time_number();
            }

            if (bits & (1 << K1_SPACE)) {
                int dc = 0;
                if (pressed & (1 << K1_LEFT)) dc = -1;
                else if (pressed & (1 << K1_RIGHT)) dc = 1;

                if (dc != 0 && move_block(grid, cur_row, cur_col, dc, &target_col)) {
                    int move_row = cur_row;
                    play_move_bop();
                    redraw_cell(grid, move_row, cur_col);   /* vacated source cell -> black */
                    redraw_cell(grid, move_row, target_col); /* the block's new (pre-fall) spot */

                    settle_animated(grid, &score);
                    draw_score_number();

                    cur_col = target_col;
                    cur_row = cursor_landing_row(grid, target_col, move_row);
                    draw_cursor(cur_row, cur_col, cursor_frame);

                    if (is_grid_empty(grid)) {
                        score += time_left * TIME_BONUS_PER_SECOND;
                        draw_score_number();
                        trigger_game_over(BANNER_LEVEL_COMPLETE);
                    } else if (!has_any_move(grid) || has_orphan_block(grid)) {
                        trigger_game_over(BANNER_GAME_OVER); /* stuck */
                    }
                }
                continue;
            }

            new_row = cur_row;
            new_col = cur_col;
            if (pressed & (1 << K1_LEFT)) new_col--;
            if (pressed & (1 << K1_RIGHT)) new_col++;
            if (pressed & (1 << K1_UP)) new_row--;
            if (pressed & (1 << K1_DOWN)) new_row++;

            if (new_col < PLAY_MIN_COL) new_col = PLAY_MIN_COL;
            if (new_col > PLAY_MAX_COL) new_col = PLAY_MAX_COL;
            if (new_row < PLAY_MIN_ROW) new_row = PLAY_MIN_ROW;
            if (new_row > PLAY_MAX_ROW) new_row = PLAY_MAX_ROW;

            if (new_row != cur_row || new_col != cur_col) {
                redraw_cell(grid, cur_row, cur_col); /* erase old cursor position */
                cur_row = new_row;
                cur_col = new_col;
                play_cursor_pop();
                draw_cursor(cur_row, cur_col, cursor_frame);
            }
        }
    }
}
