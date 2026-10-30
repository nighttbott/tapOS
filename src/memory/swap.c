#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/memory/paging.h"
#include "header/memory/swap.h"

bool handle_page_fault(uint32_t error_code) {
    uint32_t fault_address = read_cr2();
    void *page_addr = (void *) (fault_address & ~(PAGE_FRAME_SIZE - 1));
    struct PageDirectory *page_dir = paging_get_current_page_directory_addr();

    // Protection violation or kernel address: running the instruction again would not help
    if ((error_code & PAGE_FAULT_PRESENT) || fault_address >= KERNEL_VIRTUAL_ADDRESS_BASE)
        return false;

    if (fault_address >= USER_DEMAND_ZERO_LIMIT)
        return false;
    return paging_allocate_user_page_frame(page_dir, page_addr);
}
