#include "header/cpu/gdt.h"
#include "header/cpu/interrupt.h"
#include "header/stdlib/string.h"

#define GDT_TSS_INDEX       5u
#define TSS_KERNEL_STACK_SIZE 65536u

struct GlobalDescriptorTable _gdt = {
    .table = {
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
        {
            .segment_low = 0xFFFF,
            .base_low    = 0x0000,
            .base_mid    = 0x00,
            .type_bit    = 0xA,
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
        {
            .segment_low = 0xFFFF,
            .base_low    = 0x0000,
            .base_mid    = 0x00,
            .type_bit    = 0x2,
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

struct GDTR _gdt_gdtr = {
    .size    = sizeof(struct GlobalDescriptorTable) - 1,
    .address = &_gdt,
};

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
        .type_bit = 0x9,
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
