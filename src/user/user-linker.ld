/* Flat binary loaded by the kernel at virtual address 0 (see load_shell in src/kernel.c). */
ENTRY(_start)
OUTPUT_FORMAT("binary")

SECTIONS
{
    . = 0x0;

    .text :
    {
        *(.text.entry)
        *(.text*)
    }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(COMMON) *(.bss*) }

    /* Must match USER_IMAGE_SIZE in src/kernel.c: the kernel maps and zeroes only this much. */
    ASSERT(. <= 0x40000, "user program is larger than USER_IMAGE_SIZE")

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
