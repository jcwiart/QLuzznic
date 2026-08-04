/* main.c -- entry point. A normal QDOS job (C68's usual startup gives us
 * the job header and dataspace), whose only job is to hand control to
 * the hardware takeover and never look back. See asm/display.s for why
 * takeover() is a jmp, not a call-that-returns. */

extern void takeover(void (*next)(void));
extern void game_loop(void);

int main(void) {
    takeover(game_loop);
    return 0; /* unreachable */
}
