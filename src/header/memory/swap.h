#ifndef _SWAP_H
#define _SWAP_H

#include <stdint.h>
#include <stdbool.h>
#include "header/memory/paging.h"

// Swap lives right after the 4 MiB owned by EXT2 (8192 blocks), so make the disk 5 MiB
#define SWAP_FIRST_BLOCK            8192u
#define SWAP_BLOCKS_PER_PAGE        8u
#define PAGE_FRAME_ON_SWAP_MAX_COUNT 256

// Highest user address that is allocated on first access
#define USER_DEMAND_ZERO_LIMIT      0x400000u

// Page fault error code bits, Intel Manual 3a - Figure 4-12
#define PAGE_FAULT_PRESENT          0x1u

struct SwapManager {
    bool     swap_used[PAGE_FRAME_ON_SWAP_MAX_COUNT];
    uint32_t free_count;
} __attribute__((packed));

extern struct SwapManager swap_manager;

/**
 * Read and put frame back to memory
 * @param page_dir     Page directory to read, must be the active one
 * @param virtual_addr Virtual address that caused page fault
 * @return             Will return true if success, false otherwise
 */
bool swap_in(struct PageDirectory *page_dir, void *virtual_addr);

/**
 * Write frame to swap and free it
 * @param page_dir     Page directory to read, must be the active one
 * @param virtual_addr Virtual address of the page to remove from memory
 * @return             Will return true if success, false when swap is full
 */
bool swap_out(struct PageDirectory *page_dir, void *virtual_addr);

/**
 * Page replacement: move one page from memory into swap, from any page directory.
 * Pages of an inactive directory are reached by switching CR3 for a moment
 * @return Will return true if a frame was freed
 */
bool swap_replace_page(void);

// Forget a slot whose page is no longer needed
void swap_release_slot(uint32_t slot);

/**
 * read page fault address from cr2
 */
static inline uint32_t read_cr2(void) {
    uint32_t fault_address;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_address));
    return fault_address;
}

/**
 * handle page fault
 * @param error_code error code read from cpu state
 * @return           Will return true when the instruction can run again
 */
bool handle_page_fault(uint32_t error_code);

#endif
