#include "header/cpu/gdt.h"
#include "header/cpu/interrupt.h"
#include "header/stdlib/string.h"

#define GDT_TSS_INDEX       5u
// Syscalls run ext2.c on this stack, and its 512-byte block buffers nest deeper than 4 KiB
#define TSS_KERNEL_STACK_SIZE 65536u

/**
 * Global Descriptor Table (GDT) definition.
 *
 * The kernel runs in a flat 32-bit protected mode memory model:
 *   - base  = 0x00000000
 *   - limit = 0x000FFFFF with granularity = 4 KiB  => 4 GiB address space
 *
 * Initial descriptors:
 *   index 0 -> null descriptor          (selector 0x00)
 *   index 1 -> kernel code segment      (selector 0x08)
 *   index 2 -> kernel data segment      (selector 0x10)
 *
 * The three descriptors below are laid out exactly like one raw x86
 * SegmentDescriptor. Refer to Intel SDM Vol. 3A Figure 3-8.
 *
 * The raw 8-byte encoding produced by these initializers is:
 *   code : FF FF 00 00 00 9A CF 00  (access byte 0x9A = P|DPL0|S|code,read)
 *   data : FF FF 00 00 00 92 CF 00  (access byte 0x92 = P|DPL0|S|data,write)
 */

struct GlobalDescriptorTable _gdt = {
    .table = {
        /* index 0: null descriptor, required by the architecture */
        {
            .segment_low = 0x0000,
            .base_low    = 0x0000,
            .base_mid    = 0x00,
            .type_bit    = 0x0,
            .non_system  = 0,
            .DPL         = 0,
            .present     = 0,
            .limit_high  = 0x0,
            .AVL         = 0,
            .L           = 0,
            .db          = 0,
            .granularity = 0,
            .base_high   = 0x00,
        },
        /* index 1: kernel code segment, selector 0x08 */
        {
            .segment_low = 0xFFFF,  /* limit bits 0-15           */
            .base_low    = 0x0000,  /* base bits 0-15            */
            .base_mid    = 0x00,    /* base bits 16-23           */
            .type_bit    = 0xA,     /* code segment, readable    */
            .non_system  = 1,       /* code/data segment         */
            .DPL         = 0,       /* ring 0                    */
            .present     = 1,       /* present                   */
            .limit_high  = 0xF,     /* limit bits 16-19          */
            .AVL         = 0,       /* available bit             */
            .L           = 0,       /* not a 64-bit segment      */
            .db          = 1,       /* 32-bit segment            */
            .granularity = 1,       /* limit unit = 4 KiB        */
            .base_high   = 0x00,    /* base bits 24-31           */
        },
        /* index 2: kernel data segment, selector 0x10 */
        {
            .segment_low = 0xFFFF,
            .base_low    = 0x0000,
            .base_mid    = 0x00,
            .type_bit    = 0x2,     /* data segment, writable    */
            .non_system  = 1,
            .DPL         = 0,
            .present     = 1,
            .limit_high  = 0xF,
            .AVL         = 0,
            .L           = 0,
            .db          = 1,
            .granularity = 1,
            .base_high   = 0x00,
        },
        /* index 3: user code segment, selector 0x1B */
        {
            .segment_low = 0xFFFF,
            .base_low    = 0x0000,
            .base_mid    = 0x00,
            .type_bit    = 0xA,
            .non_system  = 1,
            .DPL         = 3,
            .present     = 1,
            .limit_high  = 0xF,
            .AVL         = 0,
            .L           = 0,
            .db          = 1,
            .granularity = 1,
            .base_high   = 0x00,
        },
        /* index 4: user data segment, selector 0x23 */
        {
            .segment_low = 0xFFFF,
            .base_low    = 0x0000,
            .base_mid    = 0x00,
            .type_bit    = 0x2,
            .non_system  = 1,
            .DPL         = 3,
            .present     = 1,
            .limit_high  = 0xF,
            .AVL         = 0,
            .L           = 0,
            .db          = 1,
            .granularity = 1,
            .base_high   = 0x00,
        },
    },
};

/**
 * GDTR: operand loaded through `lgdt` by load_gdt() (see kernel-entrypoint.s).
 * `size` is the table limit (last valid byte offset), not the byte count.
 * Paging is already active, so GDTR uses the higher-half linear address.
 */
struct GDTR _gdt_gdtr = {
    .size    = sizeof(struct GlobalDescriptorTable) - 1,
    .address = &_gdt,
};

/* Guard the exact wire format of the descriptor: an 8-byte layout is required. */
_Static_assert(sizeof(struct SegmentDescriptor) == 8,
               "SegmentDescriptor must be exactly 8 bytes");
#if defined(__i386__)
_Static_assert(sizeof(struct GDTR) == 6,
               "GDTR must be exactly 6 bytes (2-byte limit + 4-byte base)");
#endif

static uint8_t tss_kernel_stack[TSS_KERNEL_STACK_SIZE]
    __attribute__((aligned(16)));

void gdt_install_tss(void) {
    struct SegmentDescriptor *descriptor = &_gdt.table[GDT_TSS_INDEX];
    uint32_t base = (uint32_t)&_tss;
    uint32_t limit = sizeof(_tss) - 1u;

    *descriptor = (struct SegmentDescriptor) {
        .segment_low = (uint16_t)limit,
        .base_low = (uint16_t)base,
        .base_mid = (uint8_t)(base >> 16),
        .type_bit = 0x9, /* available 32-bit TSS */
        .non_system = 0,
        .DPL = 0,
        .present = 1,
        .limit_high = (uint8_t)(limit >> 16),
        .AVL = 0,
        .L = 0,
        .db = 0,
        .granularity = 0,
        .base_high = (uint8_t)(base >> 24),
    };
}

void initialize_tss(void) {
    memset(&_tss, 0, sizeof(_tss));
    tss_set_kernel_stack(tss_kernel_stack + sizeof(tss_kernel_stack));
    _tss.io_map_base = sizeof(_tss);
    gdt_install_tss();
    set_tss_register();
}
