#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/driver/disk.h"
#include "header/memory/paging.h"
#include "header/memory/swap.h"

struct SwapManager swap_manager = {
    .swap_used  = {false},
    .free_count = PAGE_FRAME_ON_SWAP_MAX_COUNT,
};

static uint32_t clock_hand = KERNEL_RESERVED_PAGE_FRAME_COUNT;

static uint32_t swap_slot_to_block(uint32_t slot) {
    return SWAP_FIRST_BLOCK + slot * SWAP_BLOCKS_PER_PAGE;
}

void swap_release_slot(uint32_t slot) {
    if (slot < PAGE_FRAME_ON_SWAP_MAX_COUNT && swap_manager.swap_used[slot]) {
        swap_manager.swap_used[slot] = false;
        swap_manager.free_count++;
    }
}

bool swap_out(struct PageDirectory *page_dir, void *virtual_addr) {
    struct PageTableEntry *entry = paging_get_page_table_entry(page_dir, virtual_addr);

    if (entry == NULL || !entry->flag.present_bit || swap_manager.free_count == 0)
        return false;

    uint32_t slot = 0;
    while (swap_manager.swap_used[slot])
        slot++;

    // Keep the page mapped until its data is on disk
    write_blocks(virtual_addr, swap_slot_to_block(slot), SWAP_BLOCKS_PER_PAGE);
    swap_manager.swap_used[slot] = true;
    swap_manager.free_count--;

    uint32_t page_frame_number = entry->physical_page_base_address;
    entry->flag.present_bit = false;
    entry->ignored = PTE_IGNORED_SWAPPED;
    entry->physical_page_base_address = slot;
    flush_single_tlb(virtual_addr);
    paging_release_frame(page_frame_number);
    return true;
}

bool swap_in(struct PageDirectory *page_dir, void *virtual_addr) {
    struct PageTableEntry *entry = paging_get_page_table_entry(page_dir, virtual_addr);

    if (entry == NULL || entry->flag.present_bit || !(entry->ignored & PTE_IGNORED_SWAPPED))
        return false;

    // This may replace another page, but the page tables stay in memory
    uint32_t page_frame_number = paging_acquire_frame();
    if (page_frame_number == 0)
        return false;

    uint32_t slot = entry->physical_page_base_address;
    entry->ignored = 0;
    entry->physical_page_base_address = page_frame_number;
    entry->flag = (struct PageTableEntryFlag) {
        .present_bit = true,
        .write_bit   = true,
        .us_bit      = true,
    };
    flush_single_tlb(virtual_addr);

    read_blocks(virtual_addr, swap_slot_to_block(slot), SWAP_BLOCKS_PER_PAGE);
    swap_release_slot(slot);
    paging_set_frame_owner(page_frame_number, page_dir, virtual_addr);
    return true;
}

bool swap_replace_page(void) {
    // Clock algorithm: two rounds, the first one may only clear accessed bits
    for (uint32_t step = 0; step < 2u * PAGE_FRAME_MAX_COUNT; step++) {
        uint32_t page_frame_number = clock_hand;
        const struct FrameOwner *owner = paging_get_frame_owner(page_frame_number);

        clock_hand++;
        if (clock_hand >= PAGE_FRAME_MAX_COUNT)
            clock_hand = KERNEL_RESERVED_PAGE_FRAME_COUNT;
        if (owner == NULL || owner->page_dir == NULL)
            continue;

        struct PageTableEntry *entry = paging_get_page_table_entry(owner->page_dir, owner->virtual_addr);
        if (entry == NULL)
            continue;
        if (entry->flag.accessed_bit) {
            entry->flag.accessed_bit = false;
            flush_single_tlb(owner->virtual_addr);
            continue;
        }
        return swap_out(owner->page_dir, owner->virtual_addr);
    }
    return false;
}

bool handle_page_fault(uint32_t error_code) {
    uint32_t fault_address = read_cr2();
    void *page_addr = (void *) (fault_address & ~(PAGE_FRAME_SIZE - 1));
    struct PageDirectory *page_dir = paging_get_current_page_directory_addr();

    // Protection violation or kernel address: running the instruction again would not help
    if ((error_code & PAGE_FAULT_PRESENT) || fault_address >= KERNEL_VIRTUAL_ADDRESS_BASE)
        return false;

    struct PageTableEntry *entry = paging_get_page_table_entry(page_dir, page_addr);
    if (entry != NULL && (entry->ignored & PTE_IGNORED_SWAPPED))
        return swap_in(page_dir, page_addr);

    if (fault_address >= USER_DEMAND_ZERO_LIMIT)
        return false;
    return paging_allocate_user_page_frame(page_dir, page_addr);
}
