#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/memory/paging.h"
#include "header/stdlib/string.h"

/**
 * _paging_kernel_page_directory and _paging_kernel_page_table is initialized on kernel-entrypoint.s
 */
__attribute__((aligned(0x1000))) struct PageDirectory _paging_kernel_page_directory;

__attribute__((aligned(0x1000))) struct PageTable _paging_kernel_page_table;

/**
 * Global page frame manager state tracking free/busy frames.
 */
struct PageManagerState page_manager_state = {
    .page_frame_map = {
        [0 ... KERNEL_RESERVED_PAGE_FRAME_COUNT - 1] = true,
        [KERNEL_RESERVED_PAGE_FRAME_COUNT ... PAGE_FRAME_MAX_COUNT - 1] = false
    },
    .free_page_frame_count = PAGE_FRAME_MAX_COUNT - KERNEL_RESERVED_PAGE_FRAME_COUNT,
};

void paging_initialize(void) {
    struct PageDirectoryEntry *entry =
        &_paging_kernel_page_directory.table[RECURSIVE_PAGE_DIRECTORY_INDEX];
    uint32_t physical_addr = (uint32_t) &_paging_kernel_page_directory - KERNEL_VIRTUAL_ADDRESS_BASE;

    entry->flag = (struct PageDirectoryEntryFlag) {
        .present_bit = true,
        .write_bit   = true,
    };
    entry->page_table_base_address = physical_addr >> 12;
}

struct PageDirectory *paging_get_current_page_directory_addr(void) {
    uint32_t current_page_directory_phys_addr;
    __asm__ volatile("mov %%cr3, %0" : "=r"(current_page_directory_phys_addr));
    return (struct PageDirectory *) (current_page_directory_phys_addr + KERNEL_VIRTUAL_ADDRESS_BASE);
}

struct PageTable *paging_get_page_table(struct PageDirectory *page_dir, uint32_t page_directory_index) {
    if (page_directory_index >= RECURSIVE_PAGE_DIRECTORY_INDEX || page_dir != paging_get_current_page_directory_addr())
        return NULL;

    struct PageDirectoryEntry *dir_entry = &page_dir->table[page_directory_index];
    struct PageTable *page_table = (struct PageTable *) (RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS + page_directory_index * PAGE_FRAME_SIZE);

    if (dir_entry->flag.present_bit)
        return page_table;

    for (uint32_t page_frame_number = KERNEL_RESERVED_PAGE_FRAME_COUNT;
         page_frame_number < PAGE_FRAME_MAX_COUNT;
         page_frame_number++) {
        if (!page_manager_state.page_frame_map[page_frame_number]) {
            page_manager_state.page_frame_map[page_frame_number] = true;
            page_manager_state.free_page_frame_count--;
            dir_entry->flag = (struct PageDirectoryEntryFlag) {
                .present_bit = true,
                .write_bit   = true,
                .us_bit      = true,
            };
            dir_entry->page_table_base_address = page_frame_number;
            flush_single_tlb(page_table);
            memset(page_table, 0, PAGE_FRAME_SIZE);
            return page_table;
        }
    }
    return NULL;
}

void update_page_directory_entry(
    struct PageDirectory *page_dir,
    void *physical_addr,
    void *virtual_addr,
    struct PageTableEntryFlag flag
) {
    uint32_t page_directory_index = ((uint32_t) virtual_addr >> 22) & 0x3FF;
    uint32_t page_table_index     = ((uint32_t) virtual_addr >> 12) & 0x3FF;
    struct PageTable *page_table = paging_get_page_table(page_dir, page_directory_index);

    if (page_table == NULL)
        return;
    page_table->table[page_table_index].flag = flag;
    page_table->table[page_table_index].ignored = 0;
    page_table->table[page_table_index].physical_page_base_address = (uint32_t) physical_addr >> 12;
    flush_single_tlb(virtual_addr);
}

void flush_single_tlb(void *virtual_addr) {
    __asm__ volatile("invlpg (%0)" : /* <Empty> */ : "b"(virtual_addr) : "memory");
}
