#include "header/badapple/codec.h"
#include "header/badapple/player.h"

int32_t ba_play(const struct BaPlatform *p, uint8_t *bits, uint32_t bits_capacity,
                uint8_t *buffer, uint32_t buffer_capacity, struct BaStats *stats) {
    struct BaHeader header;
    uint32_t offset = BA_HEADER_SIZE;

    *stats = (struct BaStats){0};
    if (p->read(0, buffer, BA_HEADER_SIZE) != (int32_t)BA_HEADER_SIZE || buffer_capacity < BA_HEADER_SIZE ||
        ba_parse_header(buffer, BA_HEADER_SIZE, &header) != 0) {
        return BA_ERR_FORMAT;
    }
    uint32_t count = (uint32_t)header.width * header.height;
    if ((count + 7u) / 8u > bits_capacity || (uint32_t)header.max_len + 2u > buffer_capacity) {
        return BA_ERR_MEMORY;
    }
    stats->width = header.width;
    stats->height = header.height;
    stats->fps = header.fps;
    for (uint32_t i = 0; i < (count + 7u) / 8u; i++) {
        bits[i] = 0;
    }

    uint32_t frame_ms = 1000u / header.fps;
    uint32_t start = p->now_ms();
    for (uint32_t f = 0; f < header.frames; f++) {
        if (p->stop_requested()) {
            stats->stopped = 1;
            break;
        }
        int32_t got = p->read(offset, buffer, (uint32_t)header.max_len + 2u);
        uint32_t len = got >= 2 ? ba_le(buffer, 2) : 0;
        if (got < 2 || len > header.max_len || (uint32_t)got < len + 2u ||
            ba_decode_frame(buffer + 2, len, bits, count) != 0) {
            return BA_ERR_DATA;
        }
        offset += len + 2u;
        stats->frames++;

        uint32_t due = start + f * 1000u / header.fps;
        uint32_t now = p->now_ms();
        if (now > due + frame_ms) {
            stats->dropped++;
            continue;
        }
        while (now < due) {
            p->wait();
            now = p->now_ms();
        }
        p->show(bits, header.width, header.height);
        stats->shown++;
    }
    stats->elapsed_ms = p->now_ms() - start;
    return 0;
}
