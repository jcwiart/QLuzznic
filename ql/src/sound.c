#include "sound.h"

extern void snd_beep(unsigned char *params);
extern void snd_kill(void);
extern void wait_vbl(void);

void play_tone(int pitch, int duration) {
    unsigned char params[8];

    params[0] = (unsigned char)pitch; /* pitch1 */
    params[1] = (unsigned char)pitch; /* pitch2 (same: flat tone, no sweep) */
    params[2] = 0;                    /* interval lo */
    params[3] = 0;                    /* interval hi */
    params[4] = (unsigned char)(duration & 0xff);
    params[5] = (unsigned char)((duration >> 8) & 0xff);
    params[6] = 0; /* gradient<<4 | wrap */
    params[7] = 0; /* random<<4 | fuzz */

    snd_beep(params);
}

void stop_sound(void) {
    snd_kill();
}

/* Pitch values from the QL's pitch<->frequency relationship
 * (p = 11336/f - 8, from real-hardware measurements), targeting
 * standard equal-tempered note frequencies. snd_beep doesn't block --
 * the 8049 keeps playing a note on its own after the call returns --
 * so a "melody" is just play_tone() then wait_vbl() for a number of
 * 50Hz frames before starting the next note; each note's own hardware
 * duration is set long enough to ring through that wait. */
typedef struct {
    unsigned char pitch;
    unsigned char frames; /* 50Hz frames before the next note starts */
} MelodyNote;

static void play_melody(const MelodyNote *notes, int count) {
    int i, f;
    for (i = 0; i < count; i++) {
        play_tone(notes[i].pitch, 0x0C00);
        for (f = 0; f < notes[i].frames; f++) wait_vbl();
    }
}

#define NOTE_C5 14
#define NOTE_E5 9
#define NOTE_G5 6
#define NOTE_C6 3
#define NOTE_G3 50
#define NOTE_E3 61
#define NOTE_C3 79

void play_move_bop(void) {
    /* short, low, percussive -- just a "thud", not a musical pitch */
    play_tone(70, 0x0200);
}

void play_cursor_pop(void) {
    /* lower (duller/deeper, higher pitch value = lower frequency) and
     * shorter than play_move_bop -- fires on every cursor step, so it
     * needs to stay soft/unobtrusive rather than compete with it */
    play_tone(130, 0x0080);
}

void play_win_jingle(void) {
    /* ascending arpeggio, C5-E5-G5-C6, C6 struck twice ("ta-da-DAA")
     * for a stronger landing -- only reuses already-validated pitches,
     * no new notes (see debugging-style memory: the earlier two-octave
     * rewrite added untested low notes and came back "vilain") */
    static const MelodyNote notes[] = {
        {NOTE_C5, 6}, {NOTE_E5, 6}, {NOTE_G5, 6}, {NOTE_C6, 8}, {NOTE_C6, 16},
    };
    play_melody(notes, 5);
}

void play_lose_jingle(void) {
    /* descending triad, G3-E3-C3, C3 struck twice ("womp...WOMP") for
     * a heavier landing -- same reused-pitches-only approach as the
     * win jingle above. */
    static const MelodyNote notes[] = {
        {NOTE_G3, 14}, {NOTE_E3, 14}, {NOTE_C3, 10}, {NOTE_C3, 30},
    };
    play_melody(notes, 4);
}
