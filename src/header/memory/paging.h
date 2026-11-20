#ifndef _PAGING_H
#define _PAGING_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * Note: MB often referring to MiB in context of memory management
 * Override with -DSYSTEM_MEMORY_MB=<n> and match it with QEMU -m <n>
 */
#ifndef SYSTEM_MEMORY_MB
#define SYSTEM_MEMORY_MB     128
/* --- Process-related Memory Management (Book 4.1.1) --- */
#define PAGING_DIRECTORY_TABLE_MAX_COUNT 32

/**
 * Create new page directory from a static pool of PAGING_DIRECTORY_TABLE_MAX_COUNT.
 * The kernel half (entries 0x300 to 0x3FE) is copied from the kernel page directory, so every
 * directory shares the same 4 KiB kernel page tables. Entry 1023 points to the new directory
 * itself, so its page tables are reachable through the recursive window while it is active.
 *
 * @return Pointer to page directory virtual address. Return NULL if the pool is used up
 */
struct PageDirectory *paging_create_new_page_directory(void);
/**
 * Free a page directory made by paging_create_new_page_directory, with everything it maps
 * below the kernel half: user frames, pages in swap and the page tables themselves.
 * When page_dir is active, the kernel page directory is loaded first.
 *
 * @param page_dir Pointer to page directory virtual address
 * @return         True if free operation success, false for a directory not from the pool
 */
bool paging_free_page_directory(struct PageDirectory *page_dir);
/**
 * Change active page directory (indirectly trigger TLB flush for all non-global entry)
 *
 * @note                        Assuming page directories lives in kernel memory
 * @param page_dir_virtual_addr Page directory virtual address to switch into
 */
void paging_use_page_directory(struct PageDirectory *page_dir_virtual_addr);
/**
 * Page tables are reachable only through the recursive window of the active directory, so work
 * on another directory switches to it for a moment. The kernel half is the same everywhere,
 * so kernel code, data and stack keep working.
 *
 * @return The directory that was active, to be given back to paging_use_page_directory
 */
struct PageDirectory *paging_enter_page_directory(struct PageDirectory *page_dir);

#endif

// Number of entries in a page directory
#define PAGE_ENTRY_COUNT     1024

// Page Frame (PF) Size: 4 KiB
#define PAGE_FRAME_SIZE      0x1000u

#define KERNEL_VIRTUAL_ADDRESS_BASE 0xC0000000u
#define KERNEL_RESERVED_PAGE_FRAME_COUNT (0x400000u / PAGE_FRAME_SIZE)
#define RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS 0xFFC00000u
#define RECURSIVE_PAGE_DIRECTORY_INDEX 1023u
// First page directory entry of the kernel half (0xC0000000 >> 22)
#define KERNEL_PAGE_DIRECTORY_INDEX (KERNEL_VIRTUAL_ADDRESS_BASE >> 22)

// Top 128 KiB of RAM is reserved, Book 3.1.4: usable memory ends at 0x7FE0000 for 128 MiB
#define PHYSICAL_MEMORY_TOP_HOLE 0x20000u

// Maximum usable page frame. Default count: (128 MiB - 128 KiB) / 4 KiB = 32736 frames
#define PAGE_FRAME_MAX_COUNT ((((uint32_t) SYSTEM_MEMORY_MB << 20) - PHYSICAL_MEMORY_TOP_HOLE) / PAGE_FRAME_SIZE)

// Frame accounting, defined in paging.c
extern struct PageManagerState page_manager_state;

// kernel's  page directory, using 4 KiB pages
extern struct PageDirectory _paging_kernel_page_directory;
extern struct PageTable _paging_kernel_page_table;

/**
 * Page Directory Entry Flag, only first 8 bit
 * @param present_bit       Indicate whether this entry is exist or not.
 * @param write_bit         Indicate whether this page is writable
 * @param us_bit            Allow user mode access
 * @param use_pagesize_4_mb Indicate whether this entry use 4 MB page size (set false)
 * References: Intel Manual 3a - Ch 4 Paging - Figure 4-4 PDE: 4KB page
 */
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

/**
 * Page Table Entry Flag, only first 8 bit
 * @param accessed_bit Set by CPU on access, used by page replacement
 * @param dirty_bit    Set by CPU on write
 * References: Intel Manual 3a - Ch 4 Paging - Figure 4-6 PTE: 4KB page
 */
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

/**
 * Page Directory Entry, for page size 4 KB.
 * @param page_table_base_address Frame number of the page table
 */
struct PageDirectoryEntry {
    struct PageDirectoryEntryFlag flag;
    uint32_t global_page             : 1;
    uint32_t ignored                 : 3;
    uint32_t page_table_base_address : 20;
} __attribute__((packed));

/**
 * Page Table Entry, for page size 4 KB.
 * @param physical_page_base_address Frame number, or swap slot when PTE_IGNORED_SWAPPED is set
 */
struct PageTableEntry {
    struct PageTableEntryFlag flag;
    uint32_t global_page                : 1;
    uint32_t ignored                    : 3;
    uint32_t physical_page_base_address : 20;
} __attribute__((packed));

// Stored in PageTableEntry.ignored while present_bit is 0: the page is in swap space
#define PTE_IGNORED_SWAPPED 0x1u

_Static_assert(sizeof(struct PageDirectoryEntry) == 4, "PDE must be 4 bytes");
_Static_assert(sizeof(struct PageTableEntry) == 4, "PTE must be 4 bytes");

/**
 * Page Directory, contain array of PageDirectoryEntry.
 * Warning: Address must be aligned in 4 KB, use __attribute__((aligned(0x1000)))
 */
struct PageDirectory {
    struct PageDirectoryEntry table[PAGE_ENTRY_COUNT];
} __attribute__((packed));

/**
 *  Page Table, contain array of PageTableEntry
 */
struct PageTable {
    struct PageTableEntry table[PAGE_ENTRY_COUNT];
} __attribute__((packed));

/**
 * Containing page manager states.
 *
 * @param page_frame_map Keeping track empty space. True when the page frame is currently used
 * @param free_page_frame_count Number of false entries in page_frame_map
 */
struct PageManagerState {
    bool     page_frame_map[PAGE_FRAME_MAX_COUNT];
    uint32_t free_page_frame_count;
} __attribute__((packed));

/**
 * Who uses a frame. page_dir stays NULL for frames that cannot be replaced,
 * such as page tables and frames that are being set up.
 */
struct FrameOwner {
    struct PageDirectory *page_dir;
    void                 *virtual_addr;
};

// Install the recursive entry. Call once from kernel_setup before any allocation
void paging_initialize(void);

/**
 * Edit page table entry with respective parameter
 *
 * @param page_dir      Page directory to update
 * @param physical_addr Physical address to map
 * @param virtual_addr  Virtual address to map
 * @param flag          Page entry flags
 */
void update_page_directory_entry(
    struct PageDirectory *page_dir,
    void *physical_addr,
    void *virtual_addr,
    struct PageTableEntryFlag flag
);

/**
 * Get the requested page table from respective parameter, creating it when absent
 * @param page_dir             Page directory to read
 * @param page_directory_index page table's position/index on page directory
 */
struct PageTable *paging_get_page_table(struct PageDirectory *page_dir, uint32_t page_directory_index);

/**
 * Get the entry of a virtual address without creating a page table
 * @return Entry pointer, NULL when its page table does not exist
 */
struct PageTableEntry *paging_get_page_table_entry(struct PageDirectory *page_dir, void *virtual_addr);

// Page directory that is loaded in CR3, as a kernel virtual address
struct PageDirectory *paging_get_current_page_directory_addr(void);

/**
 * Invalidate page that contain virtual address in parameter
 *
 * @param virtual_addr Virtual address to flush
 */
void flush_single_tlb(void *virtual_addr);

/* --- Frame ownership, used by swap --- */
/**
 * Take one free frame, replacing a page when none is free
 * @return Frame number, 0 when memory and swap are both full
 */
uint32_t paging_acquire_frame(void);

// Give a frame back to the free pool
void paging_release_frame(uint32_t page_frame_number);

// Make a frame replaceable. It belongs to virtual_addr on page_dir
void paging_set_frame_owner(uint32_t page_frame_number, struct PageDirectory *page_dir, void *virtual_addr);

// @return Owner of a frame, NULL for reserved or out of range frames
const struct FrameOwner *paging_get_frame_owner(uint32_t page_frame_number);

/* --- Memory Management --- */
/**
 * Check whether a certain amount of physical memory is available
 *
 * @param amount Requested amount of physical memory in bytes
 * @return       Return true when there's enough free memory available
 */
bool paging_allocate_check(uint32_t amount);

/**
 * Allocate single user page frame in page directory
 *
 * @param page_dir     Page directory to update
 * @param virtual_addr Virtual address to be allocated
 * @return             Will return true if success, false otherwise
 */
bool paging_allocate_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr);

/**
 * Deallocate single user page frame in page directory, including a page in swap
 *
 * @param page_dir      Page directory to update
 * @param virtual_addr  Virtual address to be deallocated
 * @return              Will return true if success, false otherwise
 */
bool paging_free_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr);

/* --- Process-related Memory Management (Book 4.1.1) --- */
#define PAGING_DIRECTORY_TABLE_MAX_COUNT 32

/**
 * Create new page directory from a static pool of PAGING_DIRECTORY_TABLE_MAX_COUNT.
 * The kernel half (entries 0x300 to 0x3FE) is copied from the kernel page directory, so every
 * directory shares the same 4 KiB kernel page tables. Entry 1023 points to the new directory
 * itself, so its page tables are reachable through the recursive window while it is active.
 *
 * @return Pointer to page directory virtual address. Return NULL if the pool is used up
 */
struct PageDirectory *paging_create_new_page_directory(void);
/**
 * Free a page directory made by paging_create_new_page_directory, with everything it maps
 * below the kernel half: user frames, pages in swap and the page tables themselves.
 * When page_dir is active, the kernel page directory is loaded first.
 *
 * @param page_dir Pointer to page directory virtual address
 * @return         True if free operation success, false for a directory not from the pool
 */
bool paging_free_page_directory(struct PageDirectory *page_dir);
/**
 * Change active page directory (indirectly trigger TLB flush for all non-global entry)
 *
 * @note                        Assuming page directories lives in kernel memory
 * @param page_dir_virtual_addr Page directory virtual address to switch into
 */
void paging_use_page_directory(struct PageDirectory *page_dir_virtual_addr);
/**
 * Page tables are reachable only through the recursive window of the active directory, so work
 * on another directory switches to it for a moment. The kernel half is the same everywhere,
 * so kernel code, data and stack keep working.
 *
 * @return The directory that was active, to be given back to paging_use_page_directory
 */
struct PageDirectory *paging_enter_page_directory(struct PageDirectory *page_dir);

#endif
