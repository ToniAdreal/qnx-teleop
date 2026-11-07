/* Host test main for qnx/frame.c.
 *
 * Golden vectors were produced by twin-teleop/src/telemetry.py
 * (Python reference codec) on 2026-11-07 and pasted as literals, so this
 * test cross-checks the C codec against the Python codec byte for byte.
 * Prints one PASS/FAIL line per check and exits non-zero on any failure.
 */
#include <stdio.h>
#include <string.h>

#include "frame.h"

static int failures = 0;

#define CHECK(cond, label)                                                     \
    do {                                                                       \
        if (cond) {                                                            \
            printf("PASS %s\n", label);                                       \
        } else {                                                               \
            printf("FAIL %s\n", label);                                       \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static size_t unhex(const char *hex, uint8_t *out) {
    size_t n = 0;
    while (hex[2 * n] && hex[2 * n + 1]) {
        out[n] = (uint8_t)((hexval(hex[2 * n]) << 4) | hexval(hex[2 * n + 1]));
        n++;
    }
    return n;
}

static int bytes_equal(const uint8_t *a, const uint8_t *b, size_t n) {
    return memcmp(a, b, n) == 0;
}

int main(void) {
    /* CRC-16/CCITT-FALSE check values. */
    CHECK(tt_crc16((const uint8_t *)"", 0) == 0xFFFF, "crc empty");
    CHECK(tt_crc16((const uint8_t *)"123456789", 9) == 0x29B1,
          "crc 123456789");

    /* Golden vector A: 6 joints, ts 1000000. */
    static const char *hex_a =
        "5454010640420f0000000000cdcccc3dcdcc4cbe9a99993e00000000f90fc93ff90"
        "fc9bf0000003f000000bf000000000000a03f000000c00000403f0fb8";
    /* Golden vector B: 2 joints, ts 1001000. */
    static const char *hex_b =
        "5454010228460f000000000000000000000000000000000000000000c736";
    /* Golden vector C: 16 joints, ts 2000000. */
    static const char *hex_c =
        "5454011080841e0000000000000000000ad7233c0ad7a33c8fc2f53c0ad7233dcdcc"
        "4c3d8fc2753d295c8f3d0ad7a33dec51b83dcdcccc3dae47e13d8fc2f53db81e053e29"
        "5c0f3e9a99193e000000800ad7a3bc0ad723bd8fc275bd0ad7a3bdcdccccbd8fc2f5bd"
        "295c0fbe0ad723beec5138becdcc4cbeae4761be8fc275beb81e85be295c8fbe9a9999"
        "be7b36";

    uint8_t buf[TT_MAX_FRAME_SIZE];
    uint8_t raw[TT_MAX_FRAME_SIZE];
    tt_frame_t f;

    size_t len_a = unhex(hex_a, raw);
    CHECK(len_a == 62, "vector A length");
    CHECK(tt_decode(raw, len_a, &f) == 0, "vector A decode");
    CHECK(f.timestamp_us == 1000000 && f.joint_count == 6,
          "vector A header");
    CHECK(f.positions[4] > 1.5707f && f.positions[4] < 1.5709f,
          "vector A position[4]");
    CHECK(f.torques[3] > 1.24f && f.torques[3] < 1.26f,
          "vector A torque[3]");
    CHECK(tt_encode(&f, buf) == len_a && bytes_equal(buf, raw, len_a),
          "vector A round-trip byte-identical");

    size_t len_b = unhex(hex_b, raw);
    CHECK(len_b == 30, "vector B length");
    CHECK(tt_decode(raw, len_b, &f) == 0, "vector B decode");
    CHECK(f.joint_count == 2 && f.timestamp_us == 1001000,
          "vector B header");
    CHECK(tt_encode(&f, buf) == len_b && bytes_equal(buf, raw, len_b),
          "vector B round-trip byte-identical");

    size_t len_c = unhex(hex_c, raw);
    CHECK(len_c == 142, "vector C length");
    CHECK(tt_decode(raw, len_c, &f) == 0, "vector C decode");
    CHECK(f.joint_count == 16 && f.timestamp_us == 2000000,
          "vector C header");
    CHECK(tt_encode(&f, buf) == len_c && bytes_equal(buf, raw, len_c),
          "vector C round-trip byte-identical");

    /* Corruption is rejected: flip one payload byte, CRC must fail. */
    size_t len = unhex(hex_a, raw);
    raw[20] ^= 0x01;
    CHECK(tt_decode(raw, len, &f) == -3, "corrupted frame rejected (CRC)");
    raw[20] ^= 0x01;
    raw[0] = 'X';
    CHECK(tt_decode(raw, len, &f) == -1, "bad magic rejected");
    raw[0] = 'T';
    CHECK(tt_decode(raw, len - 1, &f) == -2, "truncated frame rejected");

    /* Encode rejects out-of-range joint counts. */
    tt_frame_t bad;
    memset(&bad, 0, sizeof bad);
    bad.joint_count = 0;
    CHECK(tt_encode(&bad, buf) == 0, "encode rejects 0 joints");
    bad.joint_count = 17;
    CHECK(tt_encode(&bad, buf) == 0, "encode rejects 17 joints");

    printf(failures == 0 ? "ALL PASS\n" : "FAILURES %d\n", failures);
    return failures == 0 ? 0 : 1;
}
