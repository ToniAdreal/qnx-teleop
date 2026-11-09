/* Cross-implementation fuzz driver for qnx/frame.c.
 *
 * Reads one hex-encoded frame per line on stdin, decodes it with
 * tt_decode, re-encodes, and prints one line per input:
 *   OK <hex>    decode + re-encode succeeded, re-encoded bytes identical
 *   MISMATCH    decode succeeded but re-encode differs (should not happen)
 *   ERR <code>  tt_decode rejected the frame (see frame.h return codes)
 *
 * The Python side (tests/test_frame_cross_fuzz.py) generates the frames
 * with the twin-teleop reference codec, so OK proves the C codec agrees
 * with the Python codec on arbitrary frames, not just golden vectors.
 * Pure ISO C99; no I/O beyond stdin/stdout.
 */
#include <stdio.h>
#include <string.h>

#include "frame.h"

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int main(void) {
    char line[4 * TT_MAX_FRAME_SIZE + 8];
    while (fgets(line, sizeof line, stdin) != NULL) {
        uint8_t raw[TT_MAX_FRAME_SIZE];
        uint8_t out[TT_MAX_FRAME_SIZE];
        size_t n = 0;
        int bad_hex = 0;
        for (size_t i = 0; line[2 * i] && line[2 * i + 1]
                 && line[2 * i] != '\n'; i++) {
            int hi = hexval(line[2 * i]);
            int lo = hexval(line[2 * i + 1]);
            if (hi < 0 || lo < 0 || n >= sizeof raw) { bad_hex = 1; break; }
            raw[n++] = (uint8_t)((hi << 4) | lo);
        }
        if (bad_hex || n == 0) {
            printf("ERR -9\n");
            continue;
        }
        tt_frame_t f;
        int rc = tt_decode(raw, n, &f);
        if (rc != 0) {
            printf("ERR %d\n", rc);
            continue;
        }
        size_t m = tt_encode(&f, out);
        if (m != n || memcmp(out, raw, n) != 0) {
            printf("MISMATCH\n");
            continue;
        }
        printf("OK ");
        for (size_t i = 0; i < n; i++) printf("%02x", raw[i]);
        printf("\n");
    }
    return 0;
}
