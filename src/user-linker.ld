/* Flat binary loaded by the kernel at virtual address 0 (see load_shell in src/kernel.c). */
ENTRY(_start)

PHDRS
{
    code PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x0;

    .text :
    {
        KEEP(*(.text.entry))
        *(.text*)
    } :code
    .rodata : { *(.rodata*) } :code
    .data   : { *(.data*) } :data
    .bss    : { *(COMMON) *(.bss*) } :data

    ASSERT(_start == 0, "user entry must start at address zero")

    /* Must match USER_IMAGE_SIZE in src/kernel.c: the kernel maps and zeroes only this much. */
    ASSERT(. <= 0x40000, "user program is larger than USER_IMAGE_SIZE")

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
