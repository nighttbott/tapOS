#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "header/driver/graphics.h"
#include "header/memory/paging.h"
#include "header/cpu/portio.h"
#include "text/font8x16.h"

#define VBE_INDEX_PORT   0x01CE
#define VBE_DATA_PORT    0x01CF
#define VBE_INDEX_ID     0
#define VBE_INDEX_XRES   1
#define VBE_INDEX_YRES   2
#define VBE_INDEX_BPP    3
#define VBE_INDEX_ENABLE 4
#define VBE_ID_MIN       0xB0C0
#define VBE_ID_MAX       0xB0CF
#define VBE_DISABLED     0x00
#define VBE_ENABLED_LFB  0x41   /* enabled + linear framebuffer */

#define PCI_ADDRESS_PORT 0x0CF8
#define PCI_DATA_PORT    0x0CFC
#define PCI_VENDOR_BOCHS 0x1234
#define PCI_DEVICE_BOCHS 0x1111
#define PCI_BAR0_OFFSET  0x10

#define LFB_PAGE_COUNT   ((GRAPHICS_WIDTH * GRAPHICS_HEIGHT * 4u + PAGE_FRAME_SIZE - 1u) / PAGE_FRAME_SIZE)
#define LFB_DIRECTORY_INDEX (GRAPHICS_VIRTUAL_BASE >> 22)

_Static_assert(LFB_PAGE_COUNT <= PAGE_ENTRY_COUNT, "the framebuffer must fit in one page table");

__attribute__((aligned(PAGE_FRAME_SIZE))) static struct PageTable graphics_page_table;

/* The 16 BIOS colours (0x00RRGGBB) so attribute bytes keep their text-mode meaning. */
static const uint32_t palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

static volatile uint32_t *linear_framebuffer;

static void out32(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static uint32_t in32(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint16_t vbe_read(uint16_t index) {
    out16(VBE_INDEX_PORT, index);
    return in16(VBE_DATA_PORT);
}

static void vbe_write(uint16_t index, uint16_t value) {
    out16(VBE_INDEX_PORT, index);
    out16(VBE_DATA_PORT, value);
}

static uint32_t pci_read(uint8_t device, uint8_t offset) {
    out32(PCI_ADDRESS_PORT, 0x80000000u | ((uint32_t)device << 11) | offset);
    return in32(PCI_DATA_PORT);
}

/* The Bochs/QEMU display adapter sits on bus 0; BAR 0 is its linear framebuffer. */
static uint32_t find_framebuffer_physical(void) {
    for (uint8_t device = 0; device < 32; device++) {
        uint32_t id = pci_read(device, 0);
        if ((id & 0xFFFFu) != PCI_VENDOR_BOCHS || (id >> 16) != PCI_DEVICE_BOCHS)
            continue;
        uint32_t bar = pci_read(device, PCI_BAR0_OFFSET);
        if (bar & 1u)   /* an I/O BAR is not a framebuffer */
            return 0;
        return bar & ~0xFu;
    }
    return 0;
}

bool graphics_initialize(void) {
    uint16_t id = vbe_read(VBE_INDEX_ID);
    if (id < VBE_ID_MIN || id > VBE_ID_MAX)
        return false;

    uint32_t physical = find_framebuffer_physical();
    if (physical == 0)
        return false;

    for (uint32_t i = 0; i < LFB_PAGE_COUNT; i++) {
        graphics_page_table.table[i] = (struct PageTableEntry) {
            .flag = {.present_bit = true, .write_bit = true, .cache_disable_bit = true},
            .physical_page_base_address = (physical >> 12) + i,
        };
    }
    _paging_kernel_page_directory.table[LFB_DIRECTORY_INDEX] = (struct PageDirectoryEntry) {
        .flag = {.present_bit = true, .write_bit = true},
        .page_table_base_address =
            ((uint32_t)&graphics_page_table - KERNEL_VIRTUAL_ADDRESS_BASE) >> 12,
    };
    __asm__ volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" : : : "eax", "memory");

    vbe_write(VBE_INDEX_ENABLE, VBE_DISABLED);
    vbe_write(VBE_INDEX_XRES, GRAPHICS_WIDTH);
    vbe_write(VBE_INDEX_YRES, GRAPHICS_HEIGHT);
    vbe_write(VBE_INDEX_BPP, 32);
    vbe_write(VBE_INDEX_ENABLE, VBE_ENABLED_LFB);
    if (vbe_read(VBE_INDEX_XRES) != GRAPHICS_WIDTH || vbe_read(VBE_INDEX_YRES) != GRAPHICS_HEIGHT)
        return false;

    linear_framebuffer = (volatile uint32_t *)GRAPHICS_VIRTUAL_BASE;
    return true;
}

void graphics_draw_cell(uint8_t row, uint8_t col, uint8_t character, uint8_t fg, uint8_t bg) {
    if (linear_framebuffer == NULL)
        return;

    const uint8_t *glyph = font8x16[character];
    uint32_t foreground = palette[fg & 0x0Fu];
    uint32_t background = palette[bg & 0x0Fu];
    volatile uint32_t *top_left =
        linear_framebuffer + (uint32_t)row * GRAPHICS_GLYPH_HEIGHT * GRAPHICS_WIDTH +
        (uint32_t)col * GRAPHICS_GLYPH_WIDTH;

    for (uint32_t y = 0; y < GRAPHICS_GLYPH_HEIGHT; y++) {
        volatile uint32_t *line = top_left + y * GRAPHICS_WIDTH;
        uint8_t bits = glyph[y];
        for (uint32_t x = 0; x < GRAPHICS_GLYPH_WIDTH; x++)
            line[x] = (bits & (0x80u >> x)) ? foreground : background;
    }
}
