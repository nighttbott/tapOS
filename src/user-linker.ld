/* Based on .kit/ch4/linker.ld. The kernel loads the flat image at address 0. */
ENTRY(_start)

PHDRS
{
    code PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x0;

    .text : ALIGN(4K)
    {
        KEEP(*(.text.entry))
        *(.text*)
        *(.rodata*)
    } :code

    .data : ALIGN(4K)
    {
        *(.data*)
    } :data

    .bss : ALIGN(4K)
    {
        *(COMMON)
        *(.bss*)
    } :data

    ASSERT(_start == 0, "user entry must start at address zero")
    /* Keep this limit in sync with USER_IMAGE_SIZE in src/kernel.c. */
    ASSERT(. <= 0x40000, "user program is larger than USER_IMAGE_SIZE")

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
