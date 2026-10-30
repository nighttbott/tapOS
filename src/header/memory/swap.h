#ifndef _SWAP_H
#define _SWAP_H

#include <stdint.h>
#include <stdbool.h>
#include "header/memory/paging.h"

// Highest user address that is allocated on first access
#define USER_DEMAND_ZERO_LIMIT      0x400000u

// Page fault error code bits, Intel Manual 3a - Figure 4-12
#define PAGE_FAULT_PRESENT          0x1u

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
