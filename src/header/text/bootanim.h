#ifndef _BOOTANIM_H
#define _BOOTANIM_H

/* The logo as a 5x7 pixel font, letters t a p O S; '#' is a lit pixel. */
#define BOOT_LOGO_LETTERS 5
#define BOOT_LOGO_ROWS    7
#define BOOT_LOGO_COLS    5
extern const char *const boot_logo_letters[BOOT_LOGO_LETTERS][BOOT_LOGO_ROWS];

/**
 * Plays the start-up animation, then clears the screen: a true-colour pixel version (about
 * 4.5 seconds) when the graphics mode is on, otherwise a version drawn with text cells (about 3.8 s). Call once, with interrupts off,
 * after the framebuffer is ready. It does not depend on interrupts: it times itself by polling
 * the PIT counter.
 */
void boot_animation_play(void);

/** The pixel version (src/text/bootanim_gfx.c); needs graphics_is_active(). */
void boot_animation_play_pixels(void);

#endif
