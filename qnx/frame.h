/* TwinTeleop binary telemetry frame codec, C implementation.
 *
 * Wire format (little-endian, identical to twin-teleop/src/telemetry.py):
 *   magic        2 bytes   "TT"
 *   version      uint8     1
 *   joint_count  uint8     N (1..16)
 *   timestamp_us uint64    capture time, microseconds
 *   positions    N x float32  joint angles, rad
 *   torques      N x float32  joint torques, N*m
 *   crc16        uint16    CRC-16/CCITT-FALSE over all preceding bytes
 *
 * Pure ISO C99 + POSIX-free: compiles with gcc on a Linux host for
 * testing and with qcc under QNX SDP 8.0 for the target.
 */
#ifndef QNX_TELEOP_FRAME_H
#define QNX_TELEOP_FRAME_H

#include <stddef.h>
#include <stdint.h>

#define TT_MAGIC_0 'T'
#define TT_MAGIC_1 'T'
#define TT_VERSION 1
#define TT_MAX_JOINTS 16
#define TT_HEADER_SIZE 12 /* 2 magic + 1 version + 1 count + 8 timestamp */
#define TT_CRC_SIZE 2
#define TT_MAX_FRAME_SIZE (TT_HEADER_SIZE + 8 * TT_MAX_JOINTS + TT_CRC_SIZE)

typedef struct {
    uint64_t timestamp_us;
    uint8_t joint_count;
    float positions[TT_MAX_JOINTS];
    float torques[TT_MAX_JOINTS];
} tt_frame_t;

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF). */
uint16_t tt_crc16(const uint8_t *data, size_t len);

/* Encoded size of a frame with n joints. */
size_t tt_encoded_size(uint8_t n);

/* Encode frame into out (must hold tt_encoded_size(n) bytes). Returns
 * bytes written, or 0 on invalid input. */
size_t tt_encode(const tt_frame_t *frame, uint8_t *out);

/* Decode exactly one frame from buf of length len. Returns 0 on success
 * and fills frame; returns -1 on bad magic/version, -2 on length
 * mismatch, -3 on CRC mismatch. */
int tt_decode(const uint8_t *buf, size_t len, tt_frame_t *frame);

#endif /* QNX_TELEOP_FRAME_H */
