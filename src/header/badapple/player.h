#ifndef _BADAPPLE_PLAYER_H
#define _BADAPPLE_PLAYER_H

#include <stdint.h>

#define BA_ERR_FORMAT  (-1)   /* not a Bad Apple file */
#define BA_ERR_MEMORY  (-2)   /* the caller's buffers are too small */
#define BA_ERR_DATA    (-3)   /* a frame is cut off or does not decode */

/* What the player needs from the system. The real app fills it with syscalls, tests with fakes. */
struct BaPlatform {
    uint32_t (*now_ms)(void);
    int32_t  (*read)(uint32_t offset, void *buffer, uint32_t size);   /* bytes read, or -1 */
    int32_t  (*show)(const uint8_t *bits, uint32_t width, uint32_t height);
    int      (*stop_requested)(void);
    void     (*wait)(void);
};

struct BaStats {
    uint32_t frames;    /* frames decoded */
    uint32_t shown;     /* frames drawn */
    uint32_t dropped;   /* frames decoded but not drawn because playback was a frame behind */
    uint32_t elapsed_ms;
    uint32_t width, height, fps;
    int      stopped;
};

/* bits needs width*height/8 bytes, buffer needs max_len + 2 bytes. Returns 0 or a BA_ERR code. */
int32_t ba_play(const struct BaPlatform *platform, uint8_t *bits, uint32_t bits_capacity,
                uint8_t *buffer, uint32_t buffer_capacity, struct BaStats *stats);

#endif
