#include "text.h"

extern void draw_glyph(int x, int y, int glyph_index);
extern void draw_banner_glyph(int x, int y, int glyph_index);

/* Glyph indices are computed directly, matching tools/gen_font.py's
 * sequential layout exactly: 'A'-'Z' -> 0-25, '0'-'9' -> 26-35, then
 * '.', ':', '>' at 36-38, and a synthesised blank at 39 for space (or
 * anything else unrecognised) -- same fallback role the old font's
 * default case played, just at the new last index. */
#define FONT_BLANK_INDEX 39

void draw_char(int x, int y, char c) {
    int glyph_index;
    if (c >= 'A' && c <= 'Z') {
        glyph_index = c - 'A';
    } else if (c >= '0' && c <= '9') {
        glyph_index = 26 + (c - '0');
    } else if (c == '.') {
        glyph_index = 36;
    } else if (c == ':') {
        glyph_index = 37;
    } else if (c == '>') {
        glyph_index = 38;
    } else {
        glyph_index = FONT_BLANK_INDEX;
    }
    draw_glyph(x, y, glyph_index);
}

void draw_string(int x, int y, const char *s) {
    int i;
    for (i = 0; s[i] != '\0'; i++) {
        draw_char(x + i * GLYPH_PX, y, s[i]);
    }
}

void draw_number(int x, int y, int value) {
    char buf[12]; /* enough digits for any 32-bit int */
    int n = 0;
    int v = value;
    int i;

    if (v <= 0) {
        buf[n++] = '0';
    } else {
        while (v > 0) {
            buf[n++] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
    /* buf holds digits least-significant-first */

    for (i = 0; i < n; i++) {
        draw_char(x + i * GLYPH_PX, y, buf[n - 1 - i]);
    }
}

/* glyph_index matches draw_char's small-font layout ('A'-'Z' -> 0-25)
 * exactly -- draw_banner_glyph now reads and doubles the same
 * _font_data table live instead of a separately-stored 16x16 font,
 * so no dedicated banner ORDER table is needed any more (see
 * draw_banner_glyph's comment in asm/display.s). */
void draw_banner_char(int x, int y, char c) {
    int glyph_index;
    if (c >= 'A' && c <= 'Z') {
        glyph_index = c - 'A';
    } else {
        glyph_index = FONT_BLANK_INDEX; /* space, or anything else: blank */
    }
    draw_banner_glyph(x, y, glyph_index);
}

void draw_banner_string(int x, int y, const char *s) {
    int i;
    for (i = 0; s[i] != '\0'; i++) {
        draw_banner_char(x + i * BANNER_GLYPH_PX, y, s[i]);
    }
}
