#ifndef LEVEL_CODE_H
#define LEVEL_CODE_H

/* Encodes/decodes a level index as a 4-hex-character "continue code"
 * (see game_loop.c's level-complete banner and the title screen's
 * ENTER CODE flow). Pure XOR/shift, no multiply or divide -- unlike
 * LEVEL_COUNT's runtime '%' (already paid for elsewhere), a stray
 * multiply by a non-power-of-2 pulls in c68's .Xulmul library routine,
 * which has crashed the boot on this 128K target before (see game.h's
 * GRID_STRIDE comment) -- not worth the risk for what's just a cheap
 * scramble, not real security. */

/* level_index -> 2 raw bytes (out_bytes[0]=scrambled level,
 * out_bytes[1]=checksum). Caller renders these as 4 hex characters. */
void encode_level_code(int level_index, unsigned char *out_bytes);

/* Reverses encode_level_code(): 1 and *out_level set if the checksum
 * byte matches (whatever was typed is a code this build could have
 * produced), 0 (and *out_level untouched) otherwise. */
int decode_level_code(const unsigned char *bytes, int *out_level);

/* '0'-'9' -> 0-9, 'A'-'F' -> 10-15. Caller guarantees a valid hex char
 * (the code entry screen only ever accepts the 16 hex keys). */
int hex_digit_value(char c);

/* 0-15 -> '0'-'9'/'A'-'F'. Caller guarantees 0 <= v <= 15. */
char hex_digit_char(int v);

#endif
