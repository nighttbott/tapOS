#ifndef _PAGING_H
#define _PAGING_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef SYSTEM_MEMORY_MB
#define SYSTEM_MEMORY_MB     128
#endif

// Number of entries in a page directory
#define PAGE_ENTRY_COUNT     1024

// Page Frame (PF) Size: 4 KiB
#define PAGE_FRAME_SIZE      0x1000u

#define KERNEL_VIRTUAL_ADDRESS_BASE 0xC0000000u
#define KERNEL_RESERVED_PAGE_FRAME_COUNT (0x400000u / PAGE_FRAME_SIZE)
#define RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS 0xFFC00000u
#define RECURSIVE_PAGE_DIRECTORY_INDEX 1023u
#define PAGING_DIRECTORY_TABLE_MAX_COUNT 32u

#define PHYSICAL_MEMORY_TOP_HOLE 0x20000u

#define PAGE_FRAME_MAX_COUNT ((((uint32_t) SYSTEM_MEMORY_MB << 20) - PHYSICAL_MEMORY_TOP_HOLE) / PAGE_FRAME_SIZE)

// kernel's  page directory, using 4 KiB pages
extern struct PageDirectory _paging_kernel_page_directory;
extern struct PageTable _paging_kernel_page_table;

struct PageDirectoryEntryFlag {
    uint32_t present_bit        : 1;
    uint32_t write_bit          : 1;
    uint32_t us_bit             : 1;
    uint32_t write_through_bit  : 1;
    uint32_t cache_disable_bit  : 1;
    uint32_t accessed_bit       : 1;
    uint32_t ignored_bit        : 1;
    uint32_t use_pagesize_4_mb  : 1;
} __attribute__((packed));

struct PageTableEntryFlag {
    uint32_t present_bit        : 1;
    uint32_t write_bit          : 1;
    uint32_t us_bit             : 1;
    uint32_t write_through_bit  : 1;
    uint32_t cache_disable_bit  : 1;
    uint32_t accessed_bit       : 1;
    uint32_t dirty_bit          : 1;
    uint32_t page_attr_table    : 1;
} __attribute__((packed));

struct PageDirectoryEntry {
    struct PageDirectoryEntryFlag flag;
    uint32_t global_page             : 1;
    uint32_t ignored                 : 3;
    uint32_t page_table_base_address : 20;
} __attribute__((packed));

struct PageTableEntry {
    struct PageTableEntryFlag flag;
    uint32_t global_page                : 1;
    uint32_t ignored                    : 3;
    uint32_t physical_page_base_address : 20;
} __attribute__((packed));

#define PTE_IGNORED_SWAPPED 0x1u
#define PTE_IGNORED_PINNED  0x2u

_Static_assert(sizeof(struct PageDirectoryEntry) == 4, "PDE must be 4 bytes");
_Static_assert(sizeof(struct PageTableEntry) == 4, "PTE must be 4 bytes");

struct PageDirectory {
    struct PageDirectoryEntry table[PAGE_ENTRY_COUNT];
} __attribute__((packed));

/**
 *  Page Table, contain array of PageTableEntry
 */
struct PageTable {
    struct PageTableEntry table[PAGE_ENTRY_COUNT];
} __attribute__((packed));

struct PageManagerState {
    bool     page_frame_map[PAGE_FRAME_MAX_COUNT];
    uint32_t free_page_frame_count;
} __attribute__((packed));

struct FrameOwner {
    struct PageDirectory *page_dir;
    void                 *virtual_addr;
};

void paging_initialize(void);

void update_page_directory_entry(
    struct PageDirectory *page_dir,
    void *physical_addr,
    void *virtual_addr,
    struct PageTableEntryFlag flag
);

struct PageTable *paging_get_page_table(struct PageDirectory *page_dir, uint32_t page_directory_index);

struct PageTableEntry *paging_get_page_table_entry(struct PageDirectory *page_dir, void *virtual_addr);

/* Process directories share the higher-half mapping and own all user entries.
 * Switching and teardown require interrupts disabled on the current CPU. */
struct PageDirectory *paging_create_new_page_directory(void);
bool paging_free_page_directory(struct PageDirectory *page_dir);
void paging_use_page_directory(struct PageDirectory *page_dir);
struct PageDirectory *paging_get_current_page_directory_addr(void);

/**
 * Invalidate page that contain virtual address in parameter
 *
 * @param virtual_addr Virtual address to flush
 */
void flush_single_tlb(void *virtual_addr);

uint32_t paging_acquire_frame(void);

void paging_release_frame(uint32_t page_frame_number);

void paging_set_frame_owner(uint32_t page_frame_number, struct PageDirectory *page_dir, void *virtual_addr);

const struct FrameOwner *paging_get_frame_owner(uint32_t page_frame_number);

/* --- Memory Management --- */
/**
 * Check whether a certain amount of physical memory is available
 *
 * @param amount Requested amount of physical memory in bytes
 * @return       Return true when there's enough free memory available
 */
bool paging_allocate_check(uint32_t amount);

bool paging_allocate_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr);

bool paging_free_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr);

#endif
