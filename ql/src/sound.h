#ifndef SOUND_H
#define SOUND_H
void play_tone(int pitch, int duration);
void stop_sound(void);

/* Short percussive "bop" when a block slides into place. */
void play_move_bop(void);

/* Soft, dull "pop" when the cursor steps to a new cell (no block
 * slide) -- quieter/duller and shorter than play_move_bop() since it
 * fires on every cursor step, not just successful slides. */
void play_cursor_pop(void);

/* Little jingles for level-complete/game-over, in place of a single
 * flat beep -- a short ascending fanfare / descending "sad trombone". */
void play_win_jingle(void);
void play_lose_jingle(void);
#endif
