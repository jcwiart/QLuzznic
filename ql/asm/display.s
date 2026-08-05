; display.s -- hardware takeover + mode 8 screen access, callable from C.
;
; as68 syntax: comment = ';', sections = '.sect .text/.data/.bss',
; export a symbol with '.extern name' right before its label, C names get
; a leading underscore. Calling convention: args pushed by the caller,
; accessed at 8(a6),12(a6),... after 'link a6,#0'; return value in d0.
;
; _takeover is special: it is called normally from C (main()), but it
; never returns to its caller. Once we mask interrupts and switch onto
; our own supervisor stack, the caller's return address on the OLD stack
; is unreachable -- so instead of rts-ing back, we grab the "next"
; function pointer argument BEFORE switching stacks and jmp straight into
; it. See docs/takeover.md in tomconte/ql for the sequence this follows.

	.sect	.text

mc_stat		equ	$18063		; ZX8301 display control (write-only)
mc__m256	equ	8		; bit 3: 256-pixel / 8-colour mode
pc_intr		equ	$18021		; ZX8302 interrupt register
pc__frame	equ	3		; bit 3 = 50/60 Hz frame interrupt
scr0		equ	$20000		; screen 0 (displayed at boot)
scr_llen	equ	128		; bytes per scan line
scr_bytes	equ	32768		; 128*256

; void takeover(void (*next)(void)) -- never returns to caller
	.extern	_takeover
_takeover:
	move.l	4(sp),a0		; a0 = next, read before we trash sp
	trap	#0			; QDOS: enter supervisor mode
	move.w	#$2700,sr		; mask all interrupts -- QDOS is gone
	lea	sv_stack_top,sp		; run on our own supervisor stack
	move.b	#mc__m256,mc_stat	; mode 8, screen 0 displayed
	jmp	(a0)			; jump into the C game loop, never rts

; void wait_vbl(void) -- ack the frame bit, then block until the next one
	.extern	_wait_vbl
_wait_vbl:
	move.b	#8,pc_intr		; 1<<pc__frame: ack the frame bit
wait_vbl_loop:
	btst	#pc__frame,pc_intr
	beq	wait_vbl_loop
	rts

; void fill_screen(unsigned char even_byte, unsigned char odd_byte)
; Fills scr0 (32KB) with the given repeating byte pair -- a flat colour
; test pattern, not real tile blitting (that comes with the sprite work).
; Each char arg occupies a stack word, value in the low byte: verified by
; compiling a char-arg C function and reading the offsets c68 emits.
; d0/d1/a0/a1 are caller-saved scratch registers in this ABI (c68's own
; generated code never preserves them), so no save/restore needed here.
	.extern	_fill_screen
_fill_screen:
	link	a6,#0
	move.b	9(a6),d0		; even_byte
	lsl.w	#8,d0
	move.b	11(a6),d0		; d0.w = even_byte:odd_byte
	lea	scr0,a0
	move.w	#scr_bytes/2-1,d1	; word count - 1
fill_screen_loop:
	move.w	d0,(a0)+
	dbf	d1,fill_screen_loop
	unlk	a6
	rts

; margin_x is used by blit_tile/clear_tile/blit_tile_y/clear_tile_y/
; blit_steel_half's col->byte-offset formula (col*12+margin_x/2), which
; is only ever called with interior columns 1..9 (grid cols 0 and
; OUTER_W-1 are always steel border, rendered separately -- see
; blit_steel_vert/blit_steel_corner below, which take an explicit
; pixel X instead). left_border_x(8)/right_border_x(236) are the fixed
; positions those use. The board is still 240px wide overall (8 +
; border_col_w*2 + inner_w*tile_w = 8+24+216 = 248, plus the matching
; 8px on the right = 256) -- margin_x's value (-4) is NOT that 8px
; screen margin; it's whatever makes col*tile_w+margin_x land right
; for interior columns specifically: interior column 1 starts right
; after the 12px-wide left border, at x=8+12=20, and
; 20 = 1*24 + margin_x => margin_x = -4.
margin_x	equ	-4
border_col_w	equ	12		; left/right border column width in mode 8 pixels
left_border_x	equ	8		; fixed pixel X of the left border column
right_border_x	equ	236		; = left_border_x + border_col_w + INNER_W*tile_w
					; = 8 + 12 + 9*24 -- fixed pixel X of the right
					; border column
margin_y	equ	16		; play-field top margin for grid rows 1..7 (row*tile_h
					; still lands right for all of them -- see hud_height
					; below for why row 0 alone doesn't fit this formula).
tile_w		equ	24		; tile width in mode 8 pixels
tile_h		equ	32		; tile height: 24:32 (3:4) is what makes a tile appear
					; SQUARE on screen -- QL mode 8 pixels are physically
					; wider than tall by 4:3 (a square 256x256 pixel grid
					; stretched to fill the physical 4:3 screen), so a tile
					; needs the inverse 3:4 ratio to cancel that out
tile_row_bytes	equ	12		; tile_w/4*2: bytes per tile row (4-pixel groups * 2 planes)
tile_bytes	equ	384		; tile_h * tile_row_bytes = 32*12: bytes per tile/frame
					; in sprite_data/cursor_and_data/cursor_or_data

; HUD rework: grid rows 0 and 7 (OUTER_H-1) are *always* steel, for
; every level -- build_level()'s empty_grid() lays that border down
; unconditionally, level data never reaches it (see src/game.c). That
; guarantee is what lets us shrink just those two rows to half height
; (16px) and hand the 32px this reclaims to a HUD band at the very top
; of the screen, without ever risking a level that needs a movable
; block there. Border-row steel is drawn as a 16-row-tall strip of the
; same existing 24x32 steel sprite via blit_steel_half below -- see its
; comment for why this isn't composed from a separate smaller tile.
; Layout (256px tall):
;   y=0..31   HUD band (32px)
;   y=32..47  grid row 0, steel border, half height (16px)
;   y=48..239 grid rows 1..6, full height (32px each) -- margin_y+row*tile_h
;   y=240..255 grid row 7, steel border, half height (16px)
hud_height	equ	32		; top HUD band height; also row 0's fixed pixel Y
bottom_border_y	equ	240		; margin_y + 7*tile_h -- row 7's pixel Y (matches
					; the general formula; only row 0 needs special-casing)

; void blit_tile(int col, int row, int sprite_index)
; Opaque copy of one 24x32 mode 8 tile from sprite_data into scr0 at grid
; position (col,row). Safe because every tile position is 4-pixel
; (byte-group) aligned -- margin_x and tile_w are both multiples of 4 --
; so no partial-group masking is needed, unlike game8.asm's free-floating
; sprites (see spr_addr there). tile_w=24 isn't a power of 2, so the
; column->byte-offset math is done as col*8 + col*4 (=col*12=col*tile_w/2)
; instead of a single shift.
	.extern	_blit_tile
_blit_tile:
	link	a6,#0
	movem.l	d2-d3,-(sp)

	move.l	16(a6),d0		; sprite_index
	move.l	d0,d3
	lsl.l	#8,d0			; *256
	lsl.l	#7,d3			; *128
	add.l	d3,d0			; *384 (tile_bytes) bytes/tile
	lea	_sprite_data,a0
	adda.l	d0,a0

	move.l	12(a6),d0		; row
	lsl.l	#5,d0			; *32 (tile_h)
	add.l	#margin_y,d0		; y = margin_y + row*tile_h
	lsl.l	#7,d0			; y*128 (bytes per screen line)

	move.l	8(a6),d1		; col
	move.l	d1,d3
	lsl.l	#3,d1			; col*8
	lsl.l	#2,d3			; col*4
	add.l	d3,d1			; col*12 = col*tile_w/2
	add.l	#margin_x/2,d1		; + margin_x/2 = byte offset within line

	add.l	d1,d0			; d0 = byte offset into screen
	lea	scr0,a1
	adda.l	d0,a1

	moveq	#tile_h-1,d1
blit_tile_row_loop:
	move.l	(a0)+,d2
	move.l	d2,(a1)
	move.l	(a0)+,d2
	move.l	d2,4(a1)
	move.l	(a0)+,d2
	move.l	d2,8(a1)
	lea	scr_llen(a1),a1
	dbf	d1,blit_tile_row_loop

	movem.l	(sp)+,d2-d3
	unlk	a6
	rts

; void clear_tile(int col, int row)
; Zero-fills one 24x32 tile cell (used to erase the cursor overlay back
; to whatever was really there before re-blitting the real content, or
; to blank an empty cell). Same addressing as blit_tile.
	.extern	_clear_tile
_clear_tile:
	link	a6,#0
	move.l	d3,-(sp)

	move.l	12(a6),d0		; row
	lsl.l	#5,d0
	add.l	#margin_y,d0
	lsl.l	#7,d0

	move.l	8(a6),d1		; col
	move.l	d1,d3
	lsl.l	#3,d1
	lsl.l	#2,d3
	add.l	d3,d1
	add.l	#margin_x/2,d1

	add.l	d1,d0
	lea	scr0,a1
	adda.l	d0,a1

	moveq	#tile_h-1,d1
clear_tile_row_loop:
	clr.l	(a1)
	clr.l	4(a1)
	clr.l	8(a1)
	lea	scr_llen(a1),a1
	dbf	d1,clear_tile_row_loop

	move.l	(sp)+,d3
	unlk	a6
	rts

; void blit_tile_y(int col, int pixel_y, int sprite_index)
; Same as blit_tile, but the caller supplies an absolute screen pixel Y
; (already including margin_y) instead of a grid row -- used for fall
; animation, where a tile's vertical position passes through values that
; aren't multiples of tile_h. Safe for any integer Y: unlike X, mode 8's
; Y addressing is a plain per-scanline byte offset (y*128), no 4-pixel
; group alignment requirement.
	.extern	_blit_tile_y
_blit_tile_y:
	link	a6,#0
	movem.l	d2-d3,-(sp)

	move.l	16(a6),d0		; sprite_index
	move.l	d0,d3
	lsl.l	#8,d0
	lsl.l	#7,d3
	add.l	d3,d0			; *384 (tile_bytes)
	lea	_sprite_data,a0
	adda.l	d0,a0

	move.l	12(a6),d0		; pixel_y (absolute, includes margin)
	lsl.l	#7,d0			; y*128

	move.l	8(a6),d1		; col
	move.l	d1,d3
	lsl.l	#3,d1
	lsl.l	#2,d3
	add.l	d3,d1
	add.l	#margin_x/2,d1

	add.l	d1,d0
	lea	scr0,a1
	adda.l	d0,a1

	moveq	#tile_h-1,d1
blit_tile_y_row_loop:
	move.l	(a0)+,d2
	move.l	d2,(a1)
	move.l	(a0)+,d2
	move.l	d2,4(a1)
	move.l	(a0)+,d2
	move.l	d2,8(a1)
	lea	scr_llen(a1),a1
	dbf	d1,blit_tile_y_row_loop

	movem.l	(sp)+,d2-d3
	unlk	a6
	rts

; void clear_tile_y(int col, int pixel_y) -- see blit_tile_y.
	.extern	_clear_tile_y
_clear_tile_y:
	link	a6,#0
	move.l	d3,-(sp)

	move.l	12(a6),d0		; pixel_y (absolute, includes margin)
	lsl.l	#7,d0

	move.l	8(a6),d1		; col
	move.l	d1,d3
	lsl.l	#3,d1
	lsl.l	#2,d3
	add.l	d3,d1
	add.l	#margin_x/2,d1

	add.l	d1,d0
	lea	scr0,a1
	adda.l	d0,a1

	moveq	#tile_h-1,d1
clear_tile_y_row_loop:
	clr.l	(a1)
	clr.l	4(a1)
	clr.l	8(a1)
	lea	scr_llen(a1),a1
	dbf	d1,clear_tile_y_row_loop

	move.l	(sp)+,d3
	unlk	a6
	rts

; steel_sub_blit - local helper: copies the one fixed 12x16 steel_sub
; tile to the screen address already computed in a1. In: a1 = dest
; byte address. Trashes a0/d1/a1. Every steel cell is composed from
; 1, 2, or 4 copies of this one tile (see blit_steel_full/
; blit_steel_half/blit_steel_vert/blit_steel_corner below) so the
; texture tiles seamlessly at every border seam -- see
; tools/png2steel_sub.py. An earlier attempt at exactly this design
; reliably crashed the job at boot; that turned out to be the QL's
; 128K RAM ceiling (see game.QCF's Ram= comment), not a bug in this
; approach -- now that overall program size has come down (dead
; sidebar art removed, stack back to 2048), it's back.
steel_sub_blit:
	lea	_steel_sub_data,a0
	moveq	#15,d1			; steel_sub_h(16)-1
steel_sub_blit_row_loop:
	move.w	(a0)+,(a1)
	move.w	(a0)+,2(a1)
	move.w	(a0)+,4(a1)
	lea	scr_llen(a1),a1
	dbf	d1,steel_sub_blit_row_loop
	rts

; void blit_steel_full(int col, int row)
; Draws an ordinary (non-border) steel cell as a 2x2 mosaic of
; steel_sub tiles.
	.extern	_blit_steel_full
_blit_steel_full:
	link	a6,#0
	movem.l	d2-d4,-(sp)

	move.l	12(a6),d0		; row
	lsl.l	#5,d0			; row*32 (tile_h)
	add.l	#margin_y,d0
	lsl.l	#7,d0			; *128 -> byte line offset
	move.l	d0,d4			; d4 = base_y_bytes

	move.l	8(a6),d1		; col
	move.l	d1,d2
	lsl.l	#3,d1
	lsl.l	#2,d2
	add.l	d2,d1
	add.l	#margin_x/2,d1		; base_x_bytes
	move.l	d1,d3			; d3 = base_x_bytes

	move.l	d4,d0			; top-left
	add.l	d3,d0
	lea	scr0,a1
	adda.l	d0,a1
	bsr	steel_sub_blit

	move.l	d4,d0			; top-right (+6 bytes = 12px)
	add.l	d3,d0
	addq.l	#6,d0
	lea	scr0,a1
	adda.l	d0,a1
	bsr	steel_sub_blit

	move.l	d4,d0			; bottom-left (+16 lines)
	add.l	#2048,d0		; steel_sub_h(16)*scr_llen(128)
	add.l	d3,d0
	lea	scr0,a1
	adda.l	d0,a1
	bsr	steel_sub_blit

	move.l	d4,d0			; bottom-right
	add.l	#2048,d0
	add.l	d3,d0
	addq.l	#6,d0
	lea	scr0,a1
	adda.l	d0,a1
	bsr	steel_sub_blit

	movem.l	(sp)+,d2-d4
	unlk	a6
	rts

; void blit_steel_half(int col, int y_pixel)
; Draws a half-height border-row steel cell (row 0 or OUTER_H-1, not a
; corner) as a 1x2 mosaic. col->x uses the normal interior formula
; (blit_steel_full's), y_pixel is explicit (HUD_HEIGHT/BOTTOM_BORDER_Y
; -- these rows don't fit the general row formula, see render.h).
	.extern	_blit_steel_half
_blit_steel_half:
	link	a6,#0
	movem.l	d2-d3,-(sp)

	move.l	12(a6),d0		; y_pixel (absolute)
	lsl.l	#7,d0			; *128
	move.l	d0,d2			; d2 = base_y_bytes

	move.l	8(a6),d0		; col
	move.l	d0,d1
	lsl.l	#3,d0
	lsl.l	#2,d1
	add.l	d1,d0
	add.l	#margin_x/2,d0		; base_x_bytes
	move.l	d0,d3			; d3 = base_x_bytes

	move.l	d2,d0			; left
	add.l	d3,d0
	lea	scr0,a1
	adda.l	d0,a1
	bsr	steel_sub_blit

	move.l	d2,d0			; right
	add.l	d3,d0
	addq.l	#6,d0
	lea	scr0,a1
	adda.l	d0,a1
	bsr	steel_sub_blit

	movem.l	(sp)+,d2-d3
	unlk	a6
	rts

; void blit_steel_vert(int x_pixel, int row)
; Draws a half-width border-column steel cell (col 0 or OUTER_W-1, not
; a corner) as a 2x1 mosaic. x_pixel is explicit (LEFT_BORDER_X/
; RIGHT_BORDER_X -- border columns don't fit the general col formula
; once they're half-width, see margin_x's comment), row->y uses the
; normal interior formula (blit_steel_full's).
	.extern	_blit_steel_vert
_blit_steel_vert:
	link	a6,#0
	movem.l	d2-d3,-(sp)

	move.l	12(a6),d0		; row
	lsl.l	#5,d0			; row*32 (tile_h)
	add.l	#margin_y,d0
	lsl.l	#7,d0			; *128
	move.l	d0,d2			; d2 = base_y_bytes

	move.l	8(a6),d0		; x_pixel (absolute, multiple of 4)
	lsr.l	#1,d0			; /2 -> byte offset
	move.l	d0,d3			; d3 = base_x_bytes

	move.l	d2,d0			; top
	add.l	d3,d0
	lea	scr0,a1
	adda.l	d0,a1
	bsr	steel_sub_blit

	move.l	d2,d0			; bottom (+16 lines)
	add.l	#2048,d0
	add.l	d3,d0
	lea	scr0,a1
	adda.l	d0,a1
	bsr	steel_sub_blit

	movem.l	(sp)+,d2-d3
	unlk	a6
	rts

; void blit_steel_corner(int x_pixel, int y_pixel)
; Draws one of the four grid corners (border row and border column
; coincide) as a single steel_sub tile -- no mosaic needed, it's
; already exactly 12x16. Both X and Y explicit, same reasons as
; blit_steel_half/blit_steel_vert.
	.extern	_blit_steel_corner
_blit_steel_corner:
	link	a6,#0

	move.l	12(a6),d0		; y_pixel (absolute)
	lsl.l	#7,d0			; *128
	move.l	8(a6),d1		; x_pixel (absolute, multiple of 4)
	lsr.l	#1,d1			; /2 -> byte offset
	add.l	d1,d0
	lea	scr0,a1
	adda.l	d0,a1
	bsr	steel_sub_blit

	unlk	a6
	rts

; void draw_cursor_frame(int col, int row, int frame)
; Masked overlay blit for the marching-ants cursor: dest = (dest &
; and_mask) | or_mask, byte by byte, using the given animation frame's
; precomputed masks (see tools/png2cursor.py). Ring/corner-bevel pixels
; force black or white (their and-mask slot is 00, or-mask carries the
; colour); every other pixel is preserved (and-mask 11, or-mask 00),
; letting the real block/steel/empty content underneath show through.
; Same addressing as blit_tile. Uses a real inner loop (not unrolled,
; unlike blit_tile's row copy) since tile_w/4*2=12 repetitions is easy
; to miscount by hand.
	.extern	_draw_cursor_frame
_draw_cursor_frame:
	link	a6,#0
	movem.l	d2-d4/a2,-(sp)

	move.l	16(a6),d0		; frame
	move.l	d0,d4
	lsl.l	#8,d0
	lsl.l	#7,d4
	add.l	d4,d0			; *384 (tile_bytes) bytes/frame
	lea	_cursor_and_data,a0
	adda.l	d0,a0
	lea	_cursor_or_data,a2
	adda.l	d0,a2

	move.l	12(a6),d0		; row
	lsl.l	#5,d0
	add.l	#margin_y,d0
	lsl.l	#7,d0

	move.l	8(a6),d1		; col
	move.l	d1,d4
	lsl.l	#3,d1
	lsl.l	#2,d4
	add.l	d4,d1
	add.l	#margin_x/2,d1

	add.l	d1,d0
	lea	scr0,a1
	adda.l	d0,a1

	moveq	#tile_h-1,d1
draw_cursor_frame_row_loop:
	move.w	#tile_row_bytes-1,d4	; 12 bytes/row
draw_cursor_frame_byte_loop:
	move.b	(a1),d2
	and.b	(a0)+,d2
	or.b	(a2)+,d2
	move.b	d2,(a1)+
	dbf	d4,draw_cursor_frame_byte_loop
	lea	scr_llen-tile_row_bytes(a1),a1
	dbf	d1,draw_cursor_frame_row_loop

	movem.l	(sp)+,d2-d4/a2
	unlk	a6
	rts

; void clear_rect(int x, int y, int w, int h)
; Zero-fills an arbitrary w x h pixel rectangle at (x,y). x and w must
; be multiples of 4 (the usual mode 8 group-alignment rule); h is any
; row count. General-purpose blank-a-region primitive: used to erase
; HUD number fields before redrawing a narrower value (a shrinking
; digit count would otherwise leave stale fragments behind), and to
; erase just the exposed sliver each frame of the title logo's
; bounce-in intro (game_loop.c).
	.extern	_clear_rect
_clear_rect:
	link	a6,#0
	movem.l	d2-d4,-(sp)

	move.l	12(a6),d0		; y
	lsl.l	#7,d0			; y*128

	move.l	8(a6),d1		; x
	lsr.l	#1,d1			; x/2

	add.l	d1,d0
	lea	scr0,a1
	adda.l	d0,a1

	move.l	16(a6),d3		; w
	lsr.l	#1,d3			; bytes per row = w/2
	move.l	20(a6),d4		; h
	subq.l	#1,d4

clear_rect_row_loop:
	move.l	d3,d2
	subq.l	#1,d2
clear_rect_byte_loop:
	clr.b	(a1)+
	dbf	d2,clear_rect_byte_loop
	move.l	d3,d0
	neg.l	d0
	adda.l	d0,a1			; back up to this row's start
	lea	scr_llen(a1),a1		; advance to next row
	dbf	d4,clear_rect_row_loop

	movem.l	(sp)+,d2-d4
	unlk	a6
	rts

; packbits_next_pair - local helper shared by blit_logo_mini/
; blit_logo_big: decodes the next (even,odd) pixel-group pair from a
; PackBits-compressed stream (format documented in tools/png2logo.py).
; In: a0 = stream ptr, d4 = literal_remaining, d5 = repeat_remaining
; (caller zeroes both before the first call -- these persist in
; registers across calls, one call per pixel-group needed). Out: a0
; advanced as needed, d4/d5 updated, pair in d6(even)/d7(odd). Trashes
; nothing else -- d0-d3/a1 (the caller's row/loop bookkeeping and
; dest pointer) are untouched, so this can be bsr'd from inside the
; caller's group loop without saving/restoring anything extra.
packbits_next_pair:
	tst.b	d5
	beq	packbits_no_repeat
	subq.b	#1,d5			; still repeating the held d6/d7 pair
	rts
packbits_no_repeat:
	tst.b	d4
	beq	packbits_need_header
	subq.b	#1,d4			; still in a literal run: read a fresh pair
	move.b	(a0)+,d6
	move.b	(a0)+,d7
	rts
packbits_need_header:
	move.b	(a0)+,d5		; tentatively: repeat count (bit7=0 case)
	bmi	packbits_literal_header
	move.b	(a0)+,d6		; repeat block: read the pair to hold
	move.b	(a0)+,d7
	subq.b	#1,d5			; account for outputting it now
	rts
packbits_literal_header:
	move.b	d5,d4
	and.b	#$7f,d4			; literal count
	moveq	#0,d5			; not repeating
	subq.b	#1,d4			; account for outputting one now
	move.b	(a0)+,d6
	move.b	(a0)+,d7
	rts

; void blit_logo_mini(void)
; Draws the 100x29 mini QLuzznic logo (tools/png2logo.py, logo_mini.png,
; PackBits-compressed) at the top-left corner (0,0). Position is always
; (0,0) so no address math is needed -- dest starts at scr0 itself.
logo_mini_w_groups	equ	25		; 100/4 (100 is already a multiple of 4, no padding needed)
logo_mini_h		equ	29

	.extern	_blit_logo_mini
_blit_logo_mini:
	movem.l	d0-d7/a0-a1,-(sp)
	lea	_logo_mini_data,a0
	lea	scr0,a1
	move.l	#logo_mini_w_groups,d3
	moveq	#logo_mini_h-1,d2
	moveq	#0,d4
	moveq	#0,d5
blit_logo_mini_row_loop:
	move.l	d3,d1
	subq.l	#1,d1
blit_logo_mini_group_loop:
	bsr	packbits_next_pair
	move.b	d6,(a1)+
	move.b	d7,(a1)+
	dbf	d1,blit_logo_mini_group_loop
	move.l	d3,d0
	lsl.l	#1,d0
	neg.l	d0
	adda.l	d0,a1
	lea	scr_llen(a1),a1
	dbf	d2,blit_logo_mini_row_loop
	movem.l	(sp)+,d0-d7/a0-a1
	rts

; logo_double_table - for a 4-bit input nibble packing 2 mode-8 2-bit
; pixel fields "AB", produces the 8-bit byte "AABB" (each field
; repeated once). Used by blit_logo_big below to double every pixel
; of a decoded mode-8 byte: the byte's high nibble (pixels 0,1) and
; low nibble (pixels 2,3) each expand through this table into one
; full output byte (2 doubled pixels), so one input byte -> two
; output bytes -- see tools/png2logo.py's encode_pairs for the source
; bit layout this mirrors.
logo_double_table:
	.data1	$00,$05,$0a,$0f,$50,$55,$5a,$5f,$a0,$a5,$aa,$af,$f0,$f5,$fa,$ff

; void blit_logo_big(void)
; Draws the title screen logo by reading the SAME compressed mini
; logo stream (_logo_mini_data) as blit_logo_mini and doubling every
; pixel horizontally and vertically (100x29 -> 200x58) via
; logo_double_table above -- confirmed pixel-for-pixel identical to a
; 2x scale of the mini logo, so storing a second compressed copy was
; pure waste on a target where a few KB is the difference between
; booting and not (see [[ql-boot-crash-is-memory]]). Position (28,24)
; fixed at compile time; 200px wide centres evenly on the 256px screen
; (x=(256-200)/2=28).
;
; Each decoded (even,odd) source pair (4 source pixels) doubles into
; TWO destination groups (8 pixels): group A from each byte's high
; nibble (source pixels 0,1), group B from the low nibble (pixels
; 2,3). Both groups are written to two consecutive screen lines (a1,
; a2) to double vertically too. Row advance is a fixed compile-time
; delta (2 screen lines minus the row's own width in bytes) -- no
; runtime multiply anywhere, same discipline as every other blit here.
logo_big_w_groups	equ	logo_mini_w_groups*2	; 50 (200/4)
logo_big_h		equ	logo_mini_h*2		; 58
logo_big_x		equ	28
logo_big_row_bytes	equ	logo_mini_w_groups*4	; 2 dest groups/src group * 2 bytes, per line
logo_big_row_advance	equ	2*scr_llen - logo_big_row_bytes

; void blit_logo_big_y(int y)
; Same drawing as above, but y is a runtime pixel-Y argument instead
; of a fixed compile-time offset (x stays fixed at logo_big_x) -- like
; blit_tile_y is to blit_tile. Used by the title screen's bounce-in
; intro (game_loop.c) to redraw the logo at a different Y every frame
; from a precomputed table; the final call in that sequence lands on
; the same Y (24) the old fixed blit_logo_big used, so the settled
; logo ends up in exactly the same place.
	.extern	_blit_logo_big_y
_blit_logo_big_y:
	link	a6,#0
	movem.l	d0-d7/a0-a3,-(sp)
	lea	_logo_mini_data,a0
	move.l	8(a6),d0		; y
	lsl.l	#7,d0			; y*128
	add.l	#logo_big_x/2,d0
	lea	scr0,a1
	adda.l	d0,a1
	lea	scr_llen(a1),a2
	lea	logo_double_table,a3
	moveq	#0,d4
	moveq	#0,d5
	moveq	#logo_mini_h-1,d2
blit_logo_big_row_loop:
	moveq	#logo_mini_w_groups-1,d1
blit_logo_big_group_loop:
	bsr	packbits_next_pair		; d6=even, d7=odd (4 source pixels)

	moveq	#0,d0			; group A (pixels 0,1 doubled)
	move.b	d6,d0
	lsr.b	#4,d0
	move.b	0(a3,d0.w),d3
	move.b	d3,(a1)+
	move.b	d3,(a2)+
	moveq	#0,d0
	move.b	d7,d0
	lsr.b	#4,d0
	move.b	0(a3,d0.w),d3
	move.b	d3,(a1)+
	move.b	d3,(a2)+

	moveq	#0,d0			; group B (pixels 2,3 doubled)
	move.b	d6,d0
	and.b	#$0f,d0
	move.b	0(a3,d0.w),d3
	move.b	d3,(a1)+
	move.b	d3,(a2)+
	moveq	#0,d0
	move.b	d7,d0
	and.b	#$0f,d0
	move.b	0(a3,d0.w),d3
	move.b	d3,(a1)+
	move.b	d3,(a2)+

	dbf	d1,blit_logo_big_group_loop

	adda.l	#logo_big_row_advance,a1
	adda.l	#logo_big_row_advance,a2
	dbf	d2,blit_logo_big_row_loop
	movem.l	(sp)+,d0-d7/a0-a3
	unlk	a6
	rts

; void draw_glyph(int x, int y, int glyph_index)
; Opaque copy of one 8x7 font glyph (tools/gen_font.py, newpolice.png)
; at an absolute screen pixel position (x must be a multiple of 4 --
; caller's responsibility, same rule as every other blit here). Unlike
; blit_tile, x/y are raw screen pixels, not grid coordinates: the HUD
; text isn't on the tile grid.
	.extern	_draw_glyph
_draw_glyph:
	link	a6,#0
	movem.l	d2-d3,-(sp)

	move.l	16(a6),d0		; glyph_index
	move.l	d0,d3
	lsl.l	#5,d0			; *32
	lsl.l	#2,d3			; *4
	sub.l	d3,d0			; *32 - *4 = *28 bytes/glyph (8x7, 4 bytes/row)
	lea	_font_data,a0
	adda.l	d0,a0

	move.l	12(a6),d0		; y (absolute pixel)
	lsl.l	#7,d0			; y*128

	move.l	8(a6),d1		; x (absolute pixel, multiple of 4)
	lsr.l	#1,d1			; x/2

	add.l	d1,d0
	lea	scr0,a1
	adda.l	d0,a1

	moveq	#7-1,d1			; 7 rows, 4 bytes/row
draw_glyph_row_loop:
	move.l	(a0)+,d2
	move.l	d2,(a1)
	lea	scr_llen(a1),a1
	dbf	d1,draw_glyph_row_loop

	movem.l	(sp)+,d2-d3
	unlk	a6
	rts

; void draw_banner_glyph(int x, int y, int glyph_index)
; Opaque copy of one 16x16 banner-font glyph (tools/gen_banner_font.py)
; at an absolute screen pixel position -- same shape as draw_glyph, just
; reading the bigger font table and 16 rows/8 bytes instead of 8/4.
	.extern	_draw_banner_glyph
_draw_banner_glyph:
	link	a6,#0
	move.l	d2,-(sp)

	move.l	16(a6),d0		; glyph_index
	lsl.l	#7,d0			; *128 bytes/glyph (16x16, 8 bytes/row)
	lea	_banner_font_data,a0
	adda.l	d0,a0

	move.l	12(a6),d0		; y (absolute pixel)
	lsl.l	#7,d0			; y*128

	move.l	8(a6),d1		; x (absolute pixel, multiple of 4)
	lsr.l	#1,d1			; x/2

	add.l	d1,d0
	lea	scr0,a1
	adda.l	d0,a1

	moveq	#16-1,d1		; 16 rows, 8 bytes/row
draw_banner_glyph_row_loop:
	move.l	(a0)+,d2
	move.l	d2,(a1)
	move.l	(a0)+,d2
	move.l	d2,4(a1)
	lea	scr_llen(a1),a1
	dbf	d1,draw_banner_glyph_row_loop

	move.l	(sp)+,d2
	unlk	a6
	rts

	.sect	.bss
	.even
	ds.b	2048			; private supervisor stack -- turned out
					; the real constraint was total system
					; RAM (128K on a real QL), not this
					; stack; see game.QCF's Ram= comment
sv_stack_top:
