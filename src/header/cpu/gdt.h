#ifndef _GDT_H
#define _GDT_H

#include <stdint.h>

// Some GDT Constant
#define GDT_MAX_ENTRY_COUNT 32
/**
 * As kernel SegmentDescriptor for code located at index 1 in GDT, 
 * segment selector is sizeof(SegmentDescriptor) * 1 = 0x8
*/ 
#define GDT_KERNEL_CODE_SEGMENT_SELECTOR 0x8
#define GDT_KERNEL_DATA_SEGMENT_SELECTOR 0x10
#define GDT_USER_CODE_SEGMENT_SELECTOR   0x1B
#define GDT_USER_DATA_SEGMENT_SELECTOR   0x23
#define GDT_TSS_SEGMENT_SELECTOR         0x28

extern struct GDTR _gdt_gdtr;

/**
 * Segment Descriptor storing system segment information.
 * Struct defined exactly as Intel Manual Segment Descriptor definition (Figure 3-8 Segment Descriptor).
 * Manual can be downloaded at www.intel.com/content/www/us/en/architecture-and-technology/64-ia-32-architectures-software-developer-vol-3a-part-1-manual.html/ 
 *
 * @param segment_low  16-bit lower-bit segment limit
 * @param base_low     16-bit lower-bit base address
 * @param base_mid     8-bit middle-bit base address
 * @param type_bit     4-bit contain type flags
 * @param non_system   1-bit contain system
 */
struct SegmentDescriptor {
    // Bytes 0-3 (Bit 0 to 31): segment limit low, base address low
    uint16_t segment_low;   // Segment limit bits 0-15
    uint16_t base_low;      // Base address bits 0-15

    // Bytes 4-5 (Bit 32 to 47): base address middle, type flags
    uint8_t base_mid;       // Base address bits 16-23
    uint8_t type_bit   : 4; // Segment type (code/data/... specific)
    uint8_t non_system : 1; // S flag: 0 = system, 1 = code/data segment
    uint8_t DPL        : 2; // Descriptor Privilege Level (ring 0-3)
    uint8_t present    : 1; // P flag: 1 = segment valid & loaded in memory

    // Bytes 6-7 (Bit 48 to 63): segment limit high, flags, base address high
    uint8_t limit_high  : 4; // Segment limit bits 16-19
    uint8_t AVL         : 1; // Available for system software
    uint8_t L           : 1; // Long mode: 1 = 64-bit code segment (0 in 32-bit)
    uint8_t db          : 1; // D/B: 1 = 32-bit default operand/stack size
    uint8_t granularity : 1; // G: 1 = limit scaled by 4 KiB
    uint8_t base_high   : 8; // Base address bits 24-31
} __attribute__((packed));

/**
 * Global Descriptor Table containing list of segment descriptor. One GDT already defined in memory.c.
 * More details at https://wiki.osdev.org/GDT_Tutorial
 * @param table Fixed-width array of SegmentDescriptor with size GDT_MAX_ENTRY_COUNT
 */
struct GlobalDescriptorTable {
    struct SegmentDescriptor table[GDT_MAX_ENTRY_COUNT];
} __attribute__((packed));

/**
 * GDTR, carrying information where's the GDT located and GDT size.
 * Global kernel variable defined at memory.c.
 * 
 * @param size    Global Descriptor Table size, use sizeof operator
 * @param address GDT address, GDT should already defined properly
 */
struct GDTR {
    uint16_t                     size;
    struct GlobalDescriptorTable *address;
} __attribute__((packed));

void gdt_install_tss(void);
void initialize_tss(void);
extern void set_tss_register(void);

#endif
