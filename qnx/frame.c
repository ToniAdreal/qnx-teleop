#include "frame.h"

#include <string.h>

uint16_t tt_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

size_t tt_encoded_size(uint8_t n) {
    return TT_HEADER_SIZE + 8u * n + TT_CRC_SIZE;
}

static void put_u16le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void put_u64le(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) {
        p[i] = (uint8_t)((v >> (8 * i)) & 0xFF);
    }
}

static uint16_t get_u16le(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint64_t get_u64le(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v |= (uint64_t)p[i] << (8 * i);
    }
    return v;
}

size_t tt_encode(const tt_frame_t *frame, uint8_t *out) {
    if (!frame || !out) return 0;
    uint8_t n = frame->joint_count;
    if (n < 1 || n > TT_MAX_JOINTS) return 0;
    out[0] = TT_MAGIC_0;
    out[1] = TT_MAGIC_1;
    out[2] = TT_VERSION;
    out[3] = n;
    put_u64le(out + 4, frame->timestamp_us);
    size_t off = TT_HEADER_SIZE;
    for (uint8_t i = 0; i < n; i++) {
        uint32_t bits;
        memcpy(&bits, &frame->positions[i], 4);
        put_u16le(out + off, (uint16_t)(bits & 0xFFFF));
        put_u16le(out + off + 2, (uint16_t)((bits >> 16) & 0xFFFF));
        off += 4;
    }
    for (uint8_t i = 0; i < n; i++) {
        uint32_t bits;
        memcpy(&bits, &frame->torques[i], 4);
        put_u16le(out + off, (uint16_t)(bits & 0xFFFF));
        put_u16le(out + off + 2, (uint16_t)((bits >> 16) & 0xFFFF));
        off += 4;
    }
    uint16_t crc = tt_crc16(out, off);
    put_u16le(out + off, crc);
    return off + TT_CRC_SIZE;
}

int tt_decode(const uint8_t *buf, size_t len, tt_frame_t *frame) {
    if (!buf || !frame) return -1;
    if (len < TT_HEADER_SIZE + TT_CRC_SIZE) return -2;
    if (buf[0] != TT_MAGIC_0 || buf[1] != TT_MAGIC_1 ||
        buf[2] != TT_VERSION) {
        return -1;
    }
    uint8_t n = buf[3];
    if (n < 1 || n > TT_MAX_JOINTS) return -1;
    if (len != tt_encoded_size(n)) return -2;
    uint16_t stored = get_u16le(buf + len - TT_CRC_SIZE);
    if (tt_crc16(buf, len - TT_CRC_SIZE) != stored) return -3;
    frame->timestamp_us = get_u64le(buf + 4);
    frame->joint_count = n;
    size_t off = TT_HEADER_SIZE;
    for (uint8_t i = 0; i < n; i++) {
        uint32_t bits = (uint32_t)get_u16le(buf + off) |
                        ((uint32_t)get_u16le(buf + off + 2) << 16);
        memcpy(&frame->positions[i], &bits, 4);
        off += 4;
    }
    for (uint8_t i = 0; i < n; i++) {
        uint32_t bits = (uint32_t)get_u16le(buf + off) |
                        ((uint32_t)get_u16le(buf + off + 2) << 16);
        memcpy(&frame->torques[i], &bits, 4);
        off += 4;
    }
    return 0;
}
