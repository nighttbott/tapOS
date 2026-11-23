#ifndef _BOOTANIM_H
#define _BOOTANIM_H

/**
 * Plays the start-up animation (logo, sliding progress blocks, welcome line, curtain) on the
 * 80x25 screen, about 3.5 seconds, then clears the screen. Call once, with interrupts off,
 * after the framebuffer is ready. It does not depend on interrupts: it times itself by polling
 * the PIT counter.
 */
void boot_animation_play(void);

#endif
