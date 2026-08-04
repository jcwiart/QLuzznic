; ipc_sound.s -- direct IPC sound, takeover only. Ported from
; tomconte/ql's lib/ipc_sound_takeover.asm (snd_beep/snd_kill/ipc_byte
; only -- no snd_stat, we don't need to poll playback status), from
; vasm's Motorola syntax to as68.
;
; ipc_nib is duplicated here (see keyboard.s for the original) rather
; than called cross-file: a first version called keyboard.s's copy via
; jsr and the job crashed back to QDOS on startup (boot's "should not
; happen" line printed) -- hand-written-asm-to-hand-written-asm cross
; file calls are untested territory in this project (every other
; cross-file reference is either C-to-asm, via c68's own working jsr
; pattern, or a data lea), and this duplication sidesteps the question
; entirely while sound actually ships. Worth revisiting later if it's
; worth the risk to de-duplicate.
;
; Protocol: send command nibble (10=start sound, 11=kill), then for
; snd_beep 8 more bytes (pitch1, pitch2, interval lo/hi, duration
; lo/hi, gradient<<4|wrap, random<<4|fuzz) each as two nibbles MSB
; first. The 8049 keeps playing the sound on its own afterwards --
; snd_beep does not block for the sound's duration.

pc_ipcwr	equ	$18003	; same IPC registers as keyboard.s
pc_ipcrd	equ	$18020
inso_cmd	equ	10	; start sound
kiso_cmd	equ	11	; kill sound

	.sect	.text

; void snd_beep(unsigned char *params) -- params: the 8-byte block
; described above. d2/a3 are callee-saved in this ABI (unlike
; d0/d1/a0/a1, scratch) -- every other function here already preserves
; them when used (blit_tile, clear_tile, draw_cursor_frame...); this one
; didn't, which corrupted the caller's registers and crashed the job on
; the very first sound. Push/pop them like everywhere else.
	.extern	_snd_beep
_snd_beep:
	move.l	d2,-(sp)
	move.l	a3,-(sp)
	move.l	12(sp),a3	; params ptr (offset shifted by the 2 pushes above)
	move.w	sr,-(sp)
	ori.w	#$0700,sr	; own the link exclusively
	moveq	#inso_cmd,d0
	bsr	snd_ipc_nib	; command nibble
	moveq	#6-1,d2
snd_beep_loop:
	move.b	(a3)+,d0	; pitch1,pitch2,int_lo,int_hi,dur_lo,dur_hi
	bsr	snd_ipc_byte
	dbf	d2,snd_beep_loop
	move.b	(a3)+,d0	; gradient<<4 | wrap
	bsr	snd_ipc_byte
	move.b	(a3)+,d0	; random<<4 | fuzz
	bsr	snd_ipc_byte
	move.w	(sp)+,sr
	move.l	(sp)+,a3
	move.l	(sp)+,d2
	rts

; void snd_kill(void) -- stop sound immediately, 4 bit transactions only.
	.extern	_snd_kill
_snd_kill:
	move.w	sr,-(sp)
	ori.w	#$0700,sr
	moveq	#kiso_cmd,d0
	bsr	snd_ipc_nib
	move.w	(sp)+,sr
	rts

; snd_ipc_byte - send d0.b to the IPC, MSB first (as two nibbles).
; Interrupts must already be masked. Trashes d0/d1.
snd_ipc_byte:
	move.b	d0,-(sp)
	lsr.b	#4,d0
	bsr	snd_ipc_nib	; high nibble
	move.b	(sp)+,d0
	bsr	snd_ipc_nib	; low nibble
	rts

; snd_ipc_nib - send low nibble of d0.b to the IPC. Interrupts must
; already be masked. Trashes d0/d1. Identical to keyboard.s's ipc_nib
; (see the file header for why this is a local copy, not a shared call).
snd_ipc_nib:
	lsl.b	#4,d0		; nibble to bits 7..4 (junk above discarded)
	ori.b	#$08,d0		; end marker in bit 3
snd_ipc_nib_bit:
	lsl.b	#1,d0		; next data bit -> X
	beq	snd_ipc_nib_done ; only the marker was left: all 4 bits sent
	moveq	#$03,d1
	roxl.b	#1,d1		; %011d
	lsl.b	#1,d1		; %11d0
	move.b	d1,pc_ipcwr
snd_ipc_nib_wait:
	btst	#6,pc_ipcrd	; wait for the 8049 to take the bit
	bne	snd_ipc_nib_wait
	bra	snd_ipc_nib_bit
snd_ipc_nib_done:
	rts
