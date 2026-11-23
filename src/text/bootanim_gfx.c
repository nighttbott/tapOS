#include <stdbool.h>
#include <stdint.h>
#include "header/driver/graphics.h"
#include "header/text/bootanim.h"
#include "header/text/bootclock.h"
#include "header/text/framebuffer.h"

/*
 * Boot animation, pixel version. About 4.5 seconds, driven by the clock rather than by frame
 * counts, so a slow machine drops frames instead of running long:
 *
 *    0.00  a white line grows across a black screen and opens like a CRT turning on
 *    0.35  stars rush past at warp speed and slow down
 *    0.45  the tapOS logo fades in with a drop shadow and a reflection
 *    1.00  a shine sweeps across the logo
 *    1.20  glossy blue blocks slide along a progress track while the tagline fades in
 *    3.20  the track is replaced by "Welcome"
 *    3.80  the logo flashes
 *    4.00  the picture closes to a line like a CRT turning off
 *
 * Everything is computed per pixel (no frame buffer in RAM), and each frame redraws the logo,
 * track and text panels completely so nothing is left behind. Stars are drawn first, and the
 * panels cover them, so the stars pass behind the logo.
 */

#define W               ((int32_t) GRAPHICS_WIDTH)
#define H               ((int32_t) GRAPHICS_HEIGHT)

#define T_LINE_END       150
#define T_OPEN_END       350
#define T_LOGO_IN        450
#define T_LOGO_FULL     1000
#define T_SHINE_START   1000
#define T_SHINE_END     1600
#define T_BAR_START     1200
#define T_BAR_END       3200
#define T_WELCOME_END   3800
#define T_FLASH_END     4000
#define T_CLOSE_END     4450
#define T_END           4550

#define BLOCK           14      /* size of one logo pixel on the screen */
#define LETTER_GAP      14
#define LOGO_W          (BOOT_LOGO_LETTERS * BOOT_LOGO_COLS * BLOCK + (BOOT_LOGO_LETTERS - 1) * LETTER_GAP + BLOCK)
#define LOGO_H          (BOOT_LOGO_ROWS * BLOCK)
#define LOGO_X          ((W - LOGO_W) / 2)
#define LOGO_Y          48
#define REFLECTION_Y    (LOGO_Y + LOGO_H + 8)
#define REFLECTION_H    62
#define PANEL_PAD       18

#define TAGLINE_Y       (REFLECTION_Y + REFLECTION_H + 16)
#define BAR_W           300
#define BAR_H           22
#define BAR_X           ((W - BAR_W) / 2)
#define BAR_Y           (TAGLINE_Y + 46)
#define STATUS_Y        (BAR_Y + BAR_H + 14)
#define BAR_BLOCK_W     26
#define BAR_BLOCK_GAP   8
#define BAR_BLOCKS      3
#define BAR_SPAN        (BAR_BLOCKS * BAR_BLOCK_W + (BAR_BLOCKS - 1) * BAR_BLOCK_GAP)

#define STARS           84

#define SHINE_OFF       (-100000)
#define COLOR_TEXT      0xA8BCE0u
#define COLOR_WHITE     0xFFFFFFu

struct Star {
    int32_t x, y;
    int32_t drawn_x, drawn_length;    /* what is on the screen now, so it can be erased */
    uint32_t layer;                   /* 1 (far, slow) .. 3 (near, fast) */
    uint32_t phase;                   /* twinkle */
    bool drawn;
};

/* Panels that stars must not be drawn over: they are painted once and then only touched where
 * their content changes, so stars passing behind them cannot leave anything behind. */
struct Panel {
    int32_t x, y, width, height;
    bool enabled;
};

enum { PANEL_LOGO, PANEL_TAGLINE, PANEL_BAR, PANEL_COUNT };
static struct Panel panels[PANEL_COUNT];

static struct Star stars[STARS];
static uint32_t random_state = 0x2F6E2B1u;

static uint32_t random_next(void) {
    random_state = random_state * 1664525u + 1013904223u;
    return random_state >> 8;
}

static int32_t clamp(int32_t value, int32_t low, int32_t high) {
    return value < low ? low : value > high ? high : value;
}

/* 0..256 ease-in-out ramp: 0 before `from`, 256 after `to`. */
static uint32_t ramp(uint32_t now, uint32_t from, uint32_t to) {
    int32_t k;

    if (now <= from)
        return 0;
    if (now >= to)
        return 256;
    k = (int32_t) ((now - from) * 256u / (to - from));
    return (uint32_t) ((k * k * (3 * 256 - 2 * k)) >> 16);
}

static uint32_t mix(uint32_t a, uint32_t b, uint32_t k) {   /* k: 0 gives a, 256 gives b */
    uint32_t r = (((a >> 16) & 0xFF) * (256 - k) + ((b >> 16) & 0xFF) * k) >> 8;
    uint32_t g = (((a >> 8) & 0xFF) * (256 - k) + ((b >> 8) & 0xFF) * k) >> 8;
    uint32_t bl = ((a & 0xFF) * (256 - k) + (b & 0xFF) * k) >> 8;

    return (r << 16) | (g << 8) | bl;
}

/* Deep blue night, a little lighter towards the middle where the logo sits. */
static uint32_t background(int32_t y) {
    int32_t d = y < H / 2 ? y : H - 1 - y;

    return mix(0x01020Au, 0x0B2150u, (uint32_t) (d * 256 / (H / 2)));
}

static void panel_set(int32_t which, int32_t x, int32_t y, int32_t width, int32_t height) {
    panels[which] = (struct Panel){x, y, width, height, true};
}

/* graphics_fill_rect() for stars: skips the parts of the row that lie inside an enabled panel. */
static void fill_outside_panels(int32_t x, int32_t y, int32_t width, uint32_t color) {
    int32_t left = x;
    int32_t right = x + width;

    for (int32_t i = 0; i < PANEL_COUNT && right > left; i++) {
        const struct Panel *panel = &panels[i];
        if (!panel->enabled || y < panel->y || y >= panel->y + panel->height ||
            right <= panel->x || left >= panel->x + panel->width)
            continue;
        if (left < panel->x)
            fill_outside_panels(left, y, panel->x - left, color);
        left = panel->x + panel->width;
    }
    if (right > left)
        graphics_fill_rect(left, y, right - left, 1, color);
}

static void fill_background(int32_t x, int32_t y, int32_t width, int32_t height) {
    for (int32_t row = y; row < y + height; row++)
        graphics_fill_rect(x, row, width, 1, background(row));
}

static int32_t text_width(const char *text, uint32_t scale) {
    int32_t length = 0;

    while (text[length] != '\0')
        length++;
    return length * 8 * (int32_t) scale;
}

static void draw_text(int32_t x, int32_t y, const char *text, uint32_t scale, uint32_t color) {
    for (; *text != '\0'; text++, x += 8 * (int32_t) scale)
        graphics_draw_glyph(x, y, (uint8_t) *text, scale, color);
}

/* ---- stars ---------------------------------------------------------------------------- */

static void stars_init(void) {
    for (uint32_t i = 0; i < STARS; i++) {
        stars[i].x = (int32_t) (random_next() % (uint32_t) W);
        stars[i].y = (int32_t) (random_next() % (uint32_t) H);
        stars[i].layer = 1u + random_next() % 3u;
        stars[i].phase = random_next() & 0xFFu;
        stars[i].drawn = false;
    }
}

static void star_erase(struct Star *star) {
    if (!star->drawn)
        return;
    for (int32_t row = star->y; row < star->y + (star->layer == 3 ? 2 : 1); row++)
        fill_outside_panels(star->drawn_x - star->drawn_length, row, star->drawn_length + 2, background(row));
    star->drawn = false;
}

/* Warp: the stars streak across the screen and settle into slow twinkling points. */
static void stars_frame(uint32_t now, uint32_t dt) {
    uint32_t calm = ramp(now, T_OPEN_END, 1900);
    uint32_t speed = 256u * 14u * (256u - calm) / 256u + 256u;     /* x256 pixels per 16 ms, per layer */

    for (uint32_t i = 0; i < STARS; i++) {
        struct Star *star = &stars[i];
        int32_t step = (int32_t) ((star->layer * speed * dt) / (256u * 16u * 2u));
        int32_t length = step * 3 + 1;
        uint32_t twinkle = ((now / 9u + star->phase) & 0x7Fu);
        uint32_t base = 70u + star->layer * 50u;
        uint32_t level;

        star_erase(star);
        star->x -= step;
        if (star->x < 0) {
            star->x += W;
            star->y = (int32_t) (random_next() % (uint32_t) H);
        }
        twinkle = twinkle < 64u ? twinkle : 127u - twinkle;          /* triangle wave 0..63 */
        level = base * (160u + twinkle * 2u) / 256u;
        if (level > 255u)
            level = 255u;
        {
            uint32_t color = mix(0x000000u, 0xDCE8FFu, level);
            int32_t height = star->layer == 3 ? 2 : 1;
            int32_t third = length / 3;

            for (int32_t row = star->y; row < star->y + height; row++) {
                if (length > 1) {
                    fill_outside_panels(star->x, row, third, mix(background(row), color, 90));
                    fill_outside_panels(star->x + third, row, third, mix(background(row), color, 170));
                    fill_outside_panels(star->x + 2 * third, row, length - 2 * third, color);
                } else {
                    fill_outside_panels(star->x, row, 1, color);
                }
            }
        }
        star->drawn_x = star->x + length;
        star->drawn_length = length;
        star->drawn = true;
    }
}

/* ---- the logo ------------------------------------------------------------------------- */

/* Colour of the logo at screen position (x, y) before fading: a vertical gradient, plus the shine. */
static uint32_t logo_color(uint32_t letter, int32_t x, int32_t y, int32_t shine, uint32_t flash) {
    uint32_t down = (uint32_t) clamp((y - LOGO_Y) * 256 / LOGO_H, 0, 256);
    uint32_t color = letter < 3 ? mix(0xFFFFFFu, 0x9DB4E0u, down) : mix(0xFFF08Au, 0xF0871Au, down);

    if (shine != SHINE_OFF) {
        int32_t distance = (x + y / 2) - shine;
        if (distance < 0)
            distance = -distance;
        if (distance < 70)
            color = mix(color, COLOR_WHITE, (uint32_t) ((70 - distance) * 230 / 70));
    }
    return flash != 0 ? mix(color, COLOR_WHITE, flash) : color;
}

/* One logo pixel (a 14x14 block) at its place, or its mirror image below the logo. */
static void logo_block(uint32_t letter, int32_t x, int32_t row_in_logo, uint32_t alpha,
                       int32_t shine, uint32_t flash, bool reflection) {
    for (int32_t r = 0; r < BLOCK; r++) {
        int32_t source_y = LOGO_Y + row_in_logo * BLOCK + r;
        int32_t y = source_y;
        uint32_t a = alpha;
        uint32_t color = logo_color(letter, x + BLOCK / 2, source_y, shine, flash);

        if (reflection) {
            int32_t depth = LOGO_H - 1 - (row_in_logo * BLOCK + r);
            if (depth >= REFLECTION_H)
                continue;
            y = REFLECTION_Y + depth;
            a = alpha * (uint32_t) (80 * (REFLECTION_H - depth) / REFLECTION_H) / 256u;
        }
        if (shine != SHINE_OFF && !reflection) {
            for (int32_t c = 0; c < BLOCK; c += 2) {
                color = logo_color(letter, x + c, source_y, shine, flash);
                graphics_fill_rect(x + c, y, 2, 1, mix(background(y), color, a));
            }
        } else {
            graphics_fill_rect(x, y, BLOCK, 1, mix(background(y), color, a));
        }
    }
}

static bool logo_lit(uint32_t letter, int32_t row, int32_t col) {
    return row >= 0 && row < BOOT_LOGO_ROWS && col >= 0 && col < BOOT_LOGO_COLS &&
           boot_logo_letters[letter][row][col] == '#';
}

/* The panel is cleared by logo_panel_clear() once; every frame then repaints exactly the pixels
 * that carry the logo, its shadow and its reflection, each once, so the picture never flickers. */
static void logo_frame(uint32_t alpha, int32_t shine, uint32_t flash) {
    for (uint32_t letter = 0; letter < BOOT_LOGO_LETTERS; letter++) {
        int32_t left = LOGO_X + (int32_t) letter * (BOOT_LOGO_COLS * BLOCK + LETTER_GAP);
        for (int32_t row = 0; row < BOOT_LOGO_ROWS; row++) {
            for (int32_t col = 0; col < BOOT_LOGO_COLS; col++) {
                int32_t x = left + col * BLOCK;
                int32_t y = LOGO_Y + row * BLOCK;

                if (!logo_lit(letter, row, col))
                    continue;
                if (!logo_lit(letter, row, col + 1))     /* shadow to the right and below, where nothing covers it */
                    for (int32_t r = 6; r < BLOCK + 6; r++)
                        graphics_fill_rect(x + BLOCK, y + r, 6, 1, mix(background(y + r), 0x000000u, alpha * 150u / 256u));
                if (!logo_lit(letter, row + 1, col))
                    for (int32_t r = BLOCK; r < BLOCK + 6; r++)
                        graphics_fill_rect(x + 6, y + r, BLOCK, 1, mix(background(y + r), 0x000000u, alpha * 150u / 256u));
                logo_block(letter, x, row, alpha, shine, flash, false);
                logo_block(letter, x, row, alpha, shine, flash, true);
            }
        }
    }
}

static void logo_panel_clear(void) {
    int32_t x = LOGO_X - PANEL_PAD;
    int32_t y = LOGO_Y - 10;
    int32_t width = LOGO_W + 2 * PANEL_PAD;
    int32_t height = LOGO_H + 20 + 8 + REFLECTION_H;

    fill_background(x, y, width, height);
    panel_set(PANEL_LOGO, x, y, width, height);
}

/* ---- tagline, progress track and welcome line ----------------------------------------- */

static void tagline_frame(uint32_t alpha, bool first) {
    static const char text[] = "IF2130  Operating Systems";
    int32_t width = text_width(text, 1);

    if (first) {
        fill_background((W - width) / 2 - 8, TAGLINE_Y - 4, width + 16, 24);
        panel_set(PANEL_TAGLINE, (W - width) / 2 - 8, TAGLINE_Y - 4, width + 16, 24);
    }
    draw_text((W - width) / 2, TAGLINE_Y, text, 1, mix(background(TAGLINE_Y + 8), 0x7F93BEu, alpha));
}

#define BAR_PANEL_X      (BAR_X - 12)
#define BAR_PANEL_Y      (BAR_Y - 22)
#define BAR_PANEL_W      (BAR_W + 24)
#define BAR_PANEL_H      (BAR_H + 22 + 14 + 24 + 6)

static void bar_panel_clear(void) {
    fill_background(BAR_PANEL_X, BAR_PANEL_Y, BAR_PANEL_W, BAR_PANEL_H);
    panel_set(PANEL_BAR, BAR_PANEL_X, BAR_PANEL_Y, BAR_PANEL_W, BAR_PANEL_H);
}

/* Colour of row r of a glossy block: light on top, saturated in the middle, dark at the bottom. */
static uint32_t block_color(int32_t r, uint32_t strength) {
    uint32_t t = (uint32_t) (r * 256 / (BAR_H - 6));
    uint32_t color = t < 110 ? mix(0xA6D6FFu, 0x3F8CF2u, t * 256 / 110) : mix(0x3F8CF2u, 0x1646B8u, (t - 110) * 256 / 146);

    return mix(0x0A1630u, color, strength);
}

/* Every pixel of the track is written exactly once per frame: gaps and blocks side by side. */
static void bar_frame(uint32_t now, uint32_t alpha, bool first) {
    static const char base[] = "Starting tapOS";
    int32_t inner_x = BAR_X + 3;
    int32_t inner_right = BAR_X + BAR_W - 3;
    int32_t head = (int32_t) ((now - T_BAR_START) * 300u / 1000u % (uint32_t) (BAR_W + BAR_SPAN));
    int32_t text_x = (W - text_width("Starting tapOS...", 1)) / 2;
    uint32_t dots = (now / 280u) % 4u;
    static uint32_t drawn_dots = 99;

    if (first) {
        bar_panel_clear();
        drawn_dots = 99;
    }
    graphics_fill_rect(BAR_X, BAR_Y, BAR_W, 1, mix(background(BAR_Y), 0x8FA6D0u, alpha));
    graphics_fill_rect(BAR_X, BAR_Y + BAR_H - 1, BAR_W, 1, mix(background(BAR_Y), 0x8FA6D0u, alpha));
    graphics_fill_rect(BAR_X, BAR_Y, 1, BAR_H, mix(background(BAR_Y), 0x8FA6D0u, alpha));
    graphics_fill_rect(BAR_X + BAR_W - 1, BAR_Y, 1, BAR_H, mix(background(BAR_Y), 0x8FA6D0u, alpha));
    graphics_fill_rect(BAR_X + 1, BAR_Y + 1, BAR_W - 2, 2, mix(background(BAR_Y), 0x050B1Cu, alpha));
    graphics_fill_rect(BAR_X + 1, BAR_Y + BAR_H - 3, BAR_W - 2, 2, mix(background(BAR_Y), 0x050B1Cu, alpha));
    graphics_fill_rect(BAR_X + 1, BAR_Y + 3, 2, BAR_H - 6, mix(background(BAR_Y), 0x050B1Cu, alpha));
    graphics_fill_rect(BAR_X + BAR_W - 3, BAR_Y + 3, 2, BAR_H - 6, mix(background(BAR_Y), 0x050B1Cu, alpha));

    for (int32_t r = 0; r < BAR_H - 6; r++) {
        int32_t y = BAR_Y + 3 + r;
        int32_t cursor = inner_x;

        for (int32_t block = BAR_BLOCKS - 1; block >= 0; block--) {      /* tail first: left to right */
            int32_t x = inner_x + head - BAR_SPAN + (BAR_BLOCKS - 1 - block) * (BAR_BLOCK_W + BAR_BLOCK_GAP);
            int32_t left = x < inner_x ? inner_x : x;
            int32_t right = x + BAR_BLOCK_W > inner_right ? inner_right : x + BAR_BLOCK_W;
            uint32_t strength = alpha * (block == 0 ? 256u : block == 1 ? 200u : 140u) / 256u;

            if (right <= left)
                continue;
            if (left > cursor)
                graphics_fill_rect(cursor, y, left - cursor, 1, mix(background(y), 0x0A1630u, alpha));
            graphics_fill_rect(left, y, right - left, 1, block_color(r, strength));
            cursor = right;
        }
        if (inner_right > cursor)
            graphics_fill_rect(cursor, y, inner_right - cursor, 1, mix(background(y), 0x0A1630u, alpha));
    }

    if (dots != drawn_dots) {
        int32_t dots_x = text_x + text_width(base, 1);
        if (drawn_dots == 99)
            draw_text(text_x, STATUS_Y, base, 1, COLOR_TEXT);
        fill_background(dots_x, STATUS_Y, 24, 16);
        for (uint32_t i = 0; i < dots; i++)
            graphics_draw_glyph(dots_x + (int32_t) i * 8, STATUS_Y, '.', 1, COLOR_TEXT);
        drawn_dots = dots;
    }
}

static void welcome_frame(uint32_t alpha, bool first) {
    static const char text[] = "Welcome";
    int32_t width = text_width(text, 3);

    if (first)
        bar_panel_clear();
    draw_text((W - width) / 2, BAR_Y - 6, text, 3, mix(background(BAR_Y + 10), COLOR_WHITE, alpha));
}

/* ---- screen turning on and off ------------------------------------------------------- */

static void turn_on(uint32_t now) {
    if (now < T_LINE_END) {
        int32_t half = (int32_t) (now * (uint32_t) (W / 2) / T_LINE_END);
        graphics_fill_rect(W / 2 - half, H / 2 - 1, 2 * half, 2, COLOR_WHITE);
    } else {
        uint32_t k = ramp(now, T_LINE_END, T_OPEN_END);
        int32_t half = (int32_t) (k * (uint32_t) (H / 2) / 256u);
        for (int32_t y = H / 2 - half; y < H / 2 + half; y++) {
            int32_t from_front = y - (H / 2 - half);                 /* how far inside the opening edge */
            if (H / 2 + half - 1 - y < from_front)
                from_front = H / 2 + half - 1 - y;
            graphics_fill_rect(0, y, W, 1, from_front < 5 ? mix(background(y), COLOR_WHITE, (uint32_t) (230 - from_front * 40))
                                                           : background(y));
        }
    }
}

static void turn_off(uint32_t now) {
    uint32_t k = ramp(now, T_FLASH_END, T_CLOSE_END);
    int32_t closed = (int32_t) (k * (uint32_t) (H / 2 - 1) / 256u);

    graphics_fill_rect(0, 0, W, closed, 0x000000u);
    graphics_fill_rect(0, H - closed, W, closed, 0x000000u);
    if (closed >= H / 2 - 2) {
        int32_t half = (int32_t) ((T_END - now) * (uint32_t) (W / 2) / (T_END - T_CLOSE_END));
        graphics_fill_rect(0, H / 2 - 1, W, 2, 0x000000u);
        graphics_fill_rect(W / 2 - half, H / 2 - 1, 2 * half, 2, COLOR_WHITE);
    } else if (closed > 0) {
        graphics_fill_rect(0, closed, W, 1, mix(background(closed), COLOR_WHITE, 120));
        graphics_fill_rect(0, H - closed - 1, W, 1, mix(background(H - closed - 1), COLOR_WHITE, 120));
    }
}

void boot_animation_play_pixels(void) {
    uint32_t last = 0;
    bool logo_started = false;
    bool tagline_started = false;
    bool bar_started = false;
    bool welcome_started = false;
    uint32_t drawn_alpha = 0xFFFFFFFFu, drawn_flash = 0xFFFFFFFFu, drawn_tagline = 0xFFFFFFFFu, drawn_welcome = 0xFFFFFFFFu;
    int32_t drawn_shine = 0x7FFFFFFF;

    boot_clock_start();
    framebuffer_hide_cursor();
    graphics_fill_rect(0, 0, W, H, 0x000000u);
    stars_init();

    for (;;) {
        uint32_t now = boot_clock_ms();
        uint32_t dt = now - last;

        if (now >= T_END)
            break;
        last = now;

        if (now < T_OPEN_END) {
            turn_on(now);
        } else if (now < T_FLASH_END) {
            stars_frame(now, dt > 60u ? 60u : dt);

            if (now >= T_LOGO_IN) {
                uint32_t alpha = ramp(now, T_LOGO_IN, T_LOGO_FULL);
                int32_t shine = now >= T_SHINE_START && now < T_SHINE_END
                                    ? -80 + (int32_t) ((now - T_SHINE_START) * (uint32_t) (LOGO_X + LOGO_W + 160) / (T_SHINE_END - T_SHINE_START))
                                    : SHINE_OFF;
                uint32_t flash = now >= T_WELCOME_END ? 120u * (T_FLASH_END - now) / (T_FLASH_END - T_WELCOME_END) : 0u;

                if (!logo_started) {
                    logo_panel_clear();
                    logo_started = true;
                }
                if (alpha != drawn_alpha || shine != drawn_shine || flash != drawn_flash) {
                    logo_frame(alpha, shine, flash);
                    drawn_alpha = alpha;
                    drawn_shine = shine;
                    drawn_flash = flash;
                }
            }
            if (now >= T_SHINE_START) {
                uint32_t alpha = ramp(now, T_SHINE_START, T_SHINE_START + 400);
                if (!tagline_started || alpha != drawn_tagline) {
                    tagline_frame(alpha, !tagline_started);
                    tagline_started = true;
                    drawn_tagline = alpha;
                }
            }
            if (now >= T_BAR_START && now < T_BAR_END) {
                bar_frame(now, ramp(now, T_BAR_START, T_BAR_START + 250), !bar_started);
                bar_started = true;
            } else if (now >= T_BAR_END) {
                uint32_t alpha = ramp(now, T_BAR_END, T_BAR_END + 450);
                if (!welcome_started || alpha != drawn_welcome) {
                    welcome_frame(alpha, !welcome_started);
                    welcome_started = true;
                    drawn_welcome = alpha;
                }
            }
        } else {
            turn_off(now);
        }
    }
    framebuffer_clear();
}
