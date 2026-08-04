#ifndef TEXT_H
#define TEXT_H

#define GLYPH_PX 8 /* must match tools/gen_font.py's GLYPH_W */

/* Draws one character ('A'-'Z', '0'-'9', '.', ':', '>') at an absolute
 * screen pixel position. x must be a multiple of 4 (the mode 8 group
 * alignment rule, same as every other blit). Unsupported characters
 * (including space) draw as blank -- see tools/gen_font.py. */
void draw_char(int x, int y, char c);

/* Draws `value` as decimal digits, left-aligned starting at (x,y), no
 * padding (no leading zeros or spaces) -- caller clears the field
 * first if a shrinking value (e.g. a countdown) needs the old wider
 * text erased. */
void draw_number(int x, int y, int value);

/* Draws a string in the small font, left-aligned starting at (x,y),
 * GLYPH_PX apart per character. */
void draw_string(int x, int y, const char *s);

#define BANNER_GLYPH_PX 16 /* must match tools/gen_banner_font.py's CELL */

/* Draws one banner-font character (letters needed for "LEVEL COMPLETE"
 * + space, uppercase only) at an absolute screen pixel position. x
 * must be a multiple of 4. Unsupported characters draw as blank. */
void draw_banner_char(int x, int y, char c);

/* Draws a string in the banner font, left-aligned starting at (x,y),
 * BANNER_GLYPH_PX apart per character. */
void draw_banner_string(int x, int y, const char *s);

#endif
