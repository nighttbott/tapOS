#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/memory/paging.h"
#include "header/memory/swap.h"
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

static struct FrameOwner frame_owner[PAGE_FRAME_MAX_COUNT];

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

    // Frame owner stays empty, so a page table is never replaced
    uint32_t page_frame_number = paging_acquire_frame();
    if (page_frame_number == 0)
        return NULL;

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

struct PageTableEntry *paging_get_page_table_entry(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t page_directory_index = ((uint32_t) virtual_addr >> 22) & 0x3FF;
    uint32_t page_table_index     = ((uint32_t) virtual_addr >> 12) & 0x3FF;

    if (!page_dir->table[page_directory_index].flag.present_bit)
        return NULL;
    struct PageTable *page_table = paging_get_page_table(page_dir, page_directory_index);
    if (page_table == NULL)
        return NULL;
    return &page_table->table[page_table_index];
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



/* --- Frame ownership --- */
static uint32_t paging_find_free_frame(void) {
    for (uint32_t page_frame_number = KERNEL_RESERVED_PAGE_FRAME_COUNT;
         page_frame_number < PAGE_FRAME_MAX_COUNT;
         page_frame_number++) {
        if (!page_manager_state.page_frame_map[page_frame_number])
            return page_frame_number;
    }
    return 0;
}

uint32_t paging_acquire_frame(void) {
    uint32_t page_frame_number = paging_find_free_frame();

    if (page_frame_number == 0 && swap_replace_page())
        page_frame_number = paging_find_free_frame();
    if (page_frame_number == 0)
        return 0;

    page_manager_state.page_frame_map[page_frame_number] = true;
    page_manager_state.free_page_frame_count--;
    frame_owner[page_frame_number] = (struct FrameOwner) {NULL, NULL};
    return page_frame_number;
}

void paging_release_frame(uint32_t page_frame_number) {
    if (page_frame_number < KERNEL_RESERVED_PAGE_FRAME_COUNT || page_frame_number >= PAGE_FRAME_MAX_COUNT ||
        !page_manager_state.page_frame_map[page_frame_number])
        return;

    page_manager_state.page_frame_map[page_frame_number] = false;
    page_manager_state.free_page_frame_count++;
    frame_owner[page_frame_number] = (struct FrameOwner) {NULL, NULL};
}

void paging_set_frame_owner(uint32_t page_frame_number, struct PageDirectory *page_dir, void *virtual_addr) {
    if (page_frame_number < PAGE_FRAME_MAX_COUNT)
        frame_owner[page_frame_number] = (struct FrameOwner) {page_dir, virtual_addr};
}

const struct FrameOwner *paging_get_frame_owner(uint32_t page_frame_number) {
    if (page_frame_number >= PAGE_FRAME_MAX_COUNT)
        return NULL;
    return &frame_owner[page_frame_number];
}



/* --- Memory Management --- */
bool paging_allocate_check(uint32_t amount) {
    uint32_t required_frames = amount / PAGE_FRAME_SIZE + (amount % PAGE_FRAME_SIZE != 0);
    return required_frames <= page_manager_state.free_page_frame_count;
}

bool paging_allocate_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {
    void *page_addr = (void *) ((uint32_t) virtual_addr & ~(PAGE_FRAME_SIZE - 1));

    // Address 0 is valid, the shell lives there
    if ((uint32_t) page_addr >= KERNEL_VIRTUAL_ADDRESS_BASE)
        return false;

    struct PageTableEntry *existing = paging_get_page_table_entry(page_dir, page_addr);
    if (existing != NULL && (existing->flag.present_bit ||
                             (existing->ignored & PTE_IGNORED_SWAPPED)))
        return false;

    uint32_t page_frame_number = paging_acquire_frame();
    if (page_frame_number == 0)
        return false;
    if (paging_get_page_table(page_dir, (uint32_t) page_addr >> 22) == NULL) {
        paging_release_frame(page_frame_number);
        return false;
    }

    update_page_directory_entry(
        page_dir,
        (void *) (page_frame_number * PAGE_FRAME_SIZE),
        page_addr,
        (struct PageTableEntryFlag) {
            .present_bit = true,
            .write_bit   = true,
            .us_bit      = true,
        }
    );
    // Reachable only through its new mapping, RAM above 4 MiB is not mapped in kernel space
    memset(page_addr, 0, PAGE_FRAME_SIZE);
    paging_set_frame_owner(page_frame_number, page_dir, page_addr);
    return true;
}

bool paging_free_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {
    if ((uint32_t) virtual_addr >= KERNEL_VIRTUAL_ADDRESS_BASE)
        return false;

    struct PageTableEntry *entry = paging_get_page_table_entry(page_dir, virtual_addr);
    if (entry == NULL)
        return false;

    if (entry->flag.present_bit)
        paging_release_frame(entry->physical_page_base_address);
    else if (entry->ignored & PTE_IGNORED_SWAPPED)
        swap_release_slot(entry->physical_page_base_address);
    else
        return false;

    *entry = (struct PageTableEntry) {0};
    flush_single_tlb(virtual_addr);

    uint32_t directory_index = (uint32_t)virtual_addr >> 22;
    struct PageTable *table = paging_get_page_table(page_dir, directory_index);
    for (uint32_t i = 0; i < PAGE_ENTRY_COUNT; i++) {
        if (table->table[i].flag.present_bit ||
            (table->table[i].ignored & PTE_IGNORED_SWAPPED))
            return true;
    }

    struct PageDirectoryEntry *directory_entry = &page_dir->table[directory_index];
    uint32_t table_frame = directory_entry->page_table_base_address;
    *directory_entry = (struct PageDirectoryEntry){0};
    flush_single_tlb(table);
    paging_release_frame(table_frame);
    return true;
}
