#include "level_code.h"

/* Arbitrary fixed constants -- just enough that a code doesn't look
 * like the level number in plain hex. Not meant to resist a
 * determined player, only to feel like a "real" code and to catch
 * typos (see decode_level_code's checksum check). */
#define LEVEL_CODE_SCRAMBLE 0x5A
#define LEVEL_CODE_CHECK_XOR 0xC3

void encode_level_code(int level_index, unsigned char *out_bytes) {
    unsigned char scrambled = (unsigned char)level_index ^ LEVEL_CODE_SCRAMBLE;
    out_bytes[0] = scrambled;
    out_bytes[1] = scrambled ^ LEVEL_CODE_CHECK_XOR;
}

int decode_level_code(const unsigned char *bytes, int *out_level) {
    unsigned char scrambled = bytes[0];
    if (bytes[1] != (unsigned char)(scrambled ^ LEVEL_CODE_CHECK_XOR)) return 0;
    *out_level = scrambled ^ LEVEL_CODE_SCRAMBLE;
    return 1;
}

int hex_digit_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    return c - 'A' + 10;
}

char hex_digit_char(int v) {
    if (v < 10) return (char)('0' + v);
    return (char)('A' + v - 10);
}
