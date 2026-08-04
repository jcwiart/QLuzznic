; keyboard.s -- direct IPC keyboard read, takeover only. Ported from
; tomconte/ql's lib/ipc_keys_takeover.asm + lib/ipc_sound_takeover.asm
; (ipc_nib/ipc_rdbyte only -- no sound needed for Vexed), from vasm's
; Motorola syntax to as68 (colon labels, no dot-prefixed local labels --
; as68 has no scoped-local-label feature, so every label here is
; globally unique by name instead).
;
; IPC command 9 (KEYROW): command nibble, one 4-bit row-number parameter,
; one byte reply with the raw state of that matrix row (1 = key held).
; Row 1 (arrows/space/enter/esc) is layout-independent -- see the header
; comment in the original lib file for the AZERTY story.

pc_ipcwr	equ	$18003	; W: bit1=COMDATA, bits2,3=1, bit0=0
pc_ipcrd	equ	$18020	; R: bit6=busy, bit7=data from IPC
kbdr_cmd	equ	9	; read one keyboard row direct

key_row1	equ	1	; the cursor/space/enter/esc row
k1__enter	equ	0	; bit numbers within the row-1 reply
k1__left	equ	1
k1__up		equ	2
k1__esc		equ	3
k1__right	equ	4
k1__spc		equ	6
k1__down	equ	7

	.sect	.text

; int kbd_row(int row) -- C-callable. Returns row-1 style key bits in the
; low byte, 1 = held. Reads the arg before any push shifts the stack, so
; no link/a6 frame is needed (matches _takeover's/_wait_vbl's style).
	.extern	_kbd_row
_kbd_row:
	move.l	4(sp),d0
	bsr	kbd_row_raw
	and.l	#$ff,d0
	rts

; kbd_row_raw - read one keyboard matrix row (register convention, not
; C-callable directly). In: d0.b = row 0-7. Out: d0.b = key bits.
; Trashes d1/d2.
kbd_row_raw:
	move.w	sr,-(sp)
	ori.w	#$0700,sr	; own the link exclusively
	move.w	d0,-(sp)
	moveq	#kbdr_cmd,d0
	bsr	ipc_nib		; command nibble
	move.w	(sp)+,d0
	bsr	ipc_nib		; row-number nibble
	bsr	ipc_rdbyte	; reply: key bits -> d0.b
	move.w	(sp)+,sr
	rts

; ipc_rdbyte - read one byte from the IPC, MSB first. Returns d0.b.
; Interrupts must already be masked. Trashes d1/d2.
ipc_rdbyte:
	moveq	#0,d0
	moveq	#8-1,d2
ipc_rdbyte_loop:
	move.b	#$0e,pc_ipcwr	; %1110: assert 1 so the IPC can pull the line
ipc_rdbyte_wait:
	btst	#6,pc_ipcrd
	bne	ipc_rdbyte_wait
	move.b	pc_ipcrd,d1
	add.b	d1,d1		; bit7 -> X
	addx.b	d0,d0		; shift into result, MSB first
	dbf	d2,ipc_rdbyte_loop
	rts

; ipc_nib - send low nibble of d0.b to the IPC. Interrupts must already
; be masked. Trashes d0/d1.
; Bit pattern per JS ROM L02F7C: shift nibble to bits 7..4, OR in bit 3
; as an end marker, then shift bits out until only the marker is left.
ipc_nib:
	lsl.b	#4,d0		; nibble to bits 7..4 (junk above discarded)
	ori.b	#$08,d0		; end marker in bit 3
ipc_nib_bit:
	lsl.b	#1,d0		; next data bit -> X
	beq	ipc_nib_done	; only the marker was left: all 4 bits sent
	moveq	#$03,d1
	roxl.b	#1,d1		; %011d
	lsl.b	#1,d1		; %11d0
	move.b	d1,pc_ipcwr
ipc_nib_wait:
	btst	#6,pc_ipcrd	; wait for the 8049 to take the bit
	bne	ipc_nib_wait
	bra	ipc_nib_bit
ipc_nib_done:
	rts
