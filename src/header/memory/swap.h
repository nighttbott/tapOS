#ifndef _SWAP_H
#define _SWAP_H

#include <stdint.h>
#include <stdbool.h>
#include "header/memory/paging.h"

#define SWAP_FIRST_BLOCK            8192u
#define SWAP_BLOCKS_PER_PAGE        8u
#define PAGE_FRAME_ON_SWAP_MAX_COUNT 256

#define USER_DEMAND_ZERO_LIMIT      0x400000u

#define PAGE_FAULT_PRESENT          0x1u

struct SwapManager {
    bool     swap_used[PAGE_FRAME_ON_SWAP_MAX_COUNT];
    uint32_t free_count;
} __attribute__((packed));

extern struct SwapManager swap_manager;

bool swap_in(struct PageDirectory *page_dir, void *virtual_addr);

bool swap_out(struct PageDirectory *page_dir, void *virtual_addr);

bool swap_replace_page(void);

void swap_release_slot(uint32_t slot);

static inline uint32_t read_cr2(void) {
    uint32_t fault_address;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_address));
    return fault_address;
}

bool handle_page_fault(uint32_t error_code);

#endif
