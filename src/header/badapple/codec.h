#ifndef _BADAPPLE_CODEC_H
#define _BADAPPLE_CODEC_H

#include <stdint.h>

/*
 * Bad Apple file: 16 byte header, then one record per frame.
 *   header: "BAPL", u16 width, u16 height, u16 fps, u16 max_len, u32 frames (little endian)
 *   record: u16 len, then len bytes of run lengths (7 bit groups, low first, bit 7 = more)
 * Runs alternate black, white, black... and always start with black (the first run may be 0).
 * Pixels are bits (bit i is byte i/8, bit i%8, the layout SYSCALL_BLIT_BITS takes). Each frame is stored
 * as a change from the previous one, so a run of white means "flip these pixels". The first frame is a change from an all black screen.
 */

#define BA_HEADER_SIZE 16u

struct BaHeader {
    uint16_t width;
    uint16_t height;
    uint16_t fps;
    uint16_t max_len;
    uint32_t frames;
};

static inline uint32_t ba_le(const uint8_t *p, uint32_t bytes) {
    uint32_t v = 0;
    for (uint32_t i = 0; i < bytes; i++) {
        v |= (uint32_t)p[i] << (8u * i);
    }
    return v;
}

/* 0 on success, -1 if this is not a usable Bad Apple file. */
static inline int32_t ba_parse_header(const uint8_t *data, uint32_t size, struct BaHeader *out) {
    if (size < BA_HEADER_SIZE || data[0] != 'B' || data[1] != 'A' || data[2] != 'P' || data[3] != 'L') {
        return -1;
    }
    out->width   = (uint16_t)ba_le(data + 4, 2);
    out->height  = (uint16_t)ba_le(data + 6, 2);
    out->fps     = (uint16_t)ba_le(data + 8, 2);
    out->max_len = (uint16_t)ba_le(data + 10, 2);
    out->frames  = ba_le(data + 12, 4);
    return (out->width == 0 || out->height == 0 || out->fps == 0 || out->max_len == 0) ? -1 : 0;
}

/* Applies one frame record to the bits of count pixels. 0 on success, -1 if the runs do not fit exactly. */
static inline int32_t ba_decode_frame(const uint8_t *rle, uint32_t len, uint8_t *bits, uint32_t count) {
    uint32_t pos = 0, i = 0, white = 0;

    while (i < len) {
        uint32_t run = 0, shift = 0;
        for (;;) {
            if (i >= len || shift > 21u) {
                return -1;
            }
            uint8_t byte = rle[i++];
            run |= (uint32_t)(byte & 0x7Fu) << shift;
            shift += 7u;
            if ((byte & 0x80u) == 0) {
                break;
            }
        }
        if (run > count - pos) {
            return -1;
        }
        if (white) {
            for (uint32_t k = 0; k < run; k++) {
                bits[(pos + k) / 8u] ^= (uint8_t)(1u << ((pos + k) % 8u));
            }
        }
        pos += run;
        white ^= 1u;
    }
    return pos == count ? 0 : -1;
}

#endif
