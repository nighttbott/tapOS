#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/idt.h"
#include "header/cpu/interrupt.h"
#include "header/filesystem/ext2.h"
#include "header/memory/paging.h"
#include "header/memory/swap.h"
#include "fs_test.h"
#include "mem_cases.h"

#define MEM_TEST_BASE 0x00100000u
#define MEM_TEST_LOAD_SIZE 0x100000u
#define MEM_TEST_SWAP_PAGES (PAGE_FRAME_MAX_COUNT - KERNEL_RESERVED_PAGE_FRAME_COUNT + 64u)

extern struct PageManagerState page_manager_state;

static uint32_t page_addr(uint32_t page) {
    return MEM_TEST_BASE + page * PAGE_FRAME_SIZE;
}

// Allocate until RAM is full, free everything, then do it again
void mem_suite_frames(struct FsTestContext *ctx) {
    paging_initialize();

    struct PageDirectory *page_dir = paging_get_current_page_directory_addr();
    uint32_t free_frames_before = page_manager_state.free_page_frame_count;
    uint32_t pages = free_frames_before - 1u; // one frame goes to the page table

    fs_test_check(ctx, "M01", "check accepts free memory", paging_allocate_check(free_frames_before * PAGE_FRAME_SIZE), 1, 1);
    fs_test_check(ctx, "M02", "check rejects too much memory", !paging_allocate_check((free_frames_before + 1u) * PAGE_FRAME_SIZE), 1, 1);

    for (uint32_t round = 0; round < 2; round++) {
        bool allocated = true;
        for (uint32_t page = 0; page < pages; page++) {
            allocated = paging_allocate_user_page_frame(page_dir, (void *) page_addr(page)) && allocated;
            if (allocated)
                *(volatile uint32_t *) page_addr(page) = page;
        }
        fs_test_check(ctx, "M03", "every page can be allocated", allocated, 1, allocated);

        bool intact = true;
        for (uint32_t page = 0; page < pages; page++)
            intact = *(volatile uint32_t *) page_addr(page) == page && intact;
        fs_test_check(ctx, "M04", "pages do not share a frame", intact, 1, intact);

        uint32_t free_before_duplicate = page_manager_state.free_page_frame_count;
        bool duplicate_refused = !paging_allocate_user_page_frame(page_dir, (void *) page_addr(1));
        fs_test_check(ctx, "M10", "duplicate allocation leaves data and frame count intact",
                      duplicate_refused && *(volatile uint32_t *) page_addr(1) == 1 &&
                      page_manager_state.free_page_frame_count == free_before_duplicate, 1, duplicate_refused);

        bool replaced = paging_allocate_user_page_frame(page_dir, (void *) page_addr(pages));
        fs_test_check(ctx, "M05", "full memory replaces a page", replaced, 1, replaced);

        bool freed = true;
        for (uint32_t page = 0; page <= pages; page++)
            freed = paging_free_user_page_frame(page_dir, (void *) page_addr(page)) && freed;
        fs_test_check(ctx, "M06", "every page can be freed", freed, 1, freed);
        fs_test_check(ctx, "M07", "no frame leaked", page_manager_state.free_page_frame_count == free_frames_before,
                      (int32_t) free_frames_before, (int32_t) page_manager_state.free_page_frame_count);
    }

    fs_test_check(ctx, "M08", "kernel address is refused", !paging_allocate_user_page_frame(page_dir, (void *) KERNEL_VIRTUAL_ADDRESS_BASE), 1, 1);
    fs_test_check(ctx, "M09", "freeing twice is refused", !paging_free_user_page_frame(page_dir, (void *) page_addr(0)), 1, 1);
}

// Touch pages that were never allocated, the page fault handler has to map them
void mem_suite_fault(struct FsTestContext *ctx) {
    pic_remap();
    initialize_idt();
    paging_initialize();

    struct PageDirectory *page_dir = paging_get_current_page_directory_addr();
    uint32_t free_frames_before = page_manager_state.free_page_frame_count;
    volatile uint32_t *read_page  = (volatile uint32_t *) page_addr(0);
    volatile uint32_t *write_page = (volatile uint32_t *) page_addr(1);

    fs_test_check(ctx, "F01", "first read gives a zeroed page", *read_page == 0, 0, (int32_t) *read_page);

    *write_page = 0x12345678u;
    fs_test_check(ctx, "F02", "first write is kept", *write_page == 0x12345678u, 0x12345678, (int32_t) *write_page);

    // Two pages and their page table
    fs_test_check(ctx, "F03", "each fault takes one frame", page_manager_state.free_page_frame_count == free_frames_before - 3u,
                  (int32_t) free_frames_before - 3, (int32_t) page_manager_state.free_page_frame_count);

    bool freed = paging_free_user_page_frame(page_dir, (void *) page_addr(0)) &&
                 paging_free_user_page_frame(page_dir, (void *) page_addr(1));
    fs_test_check(ctx, "F04", "faulted pages can be freed", freed, 1, freed);
}

static uint32_t swap_pattern(uint32_t page, uint32_t offset) {
    return (page * 2654435761u) ^ (offset * 40503u) ^ 0x5A5AA5A5u;
}

// More pages than RAM can hold: every extra page has to go through swap
void mem_suite_swap(struct FsTestContext *ctx) {
    pic_remap();
    initialize_idt();
    paging_initialize();

    struct PageDirectory *page_dir = paging_get_current_page_directory_addr();
    uint32_t free_frames_before = page_manager_state.free_page_frame_count;
    uint32_t pages = MEM_TEST_SWAP_PAGES;
    int32_t bad_page = -1;

    fs_test_check(ctx, "S01", "test uses more pages than RAM has", pages > free_frames_before, 1, pages > free_frames_before);

    for (uint32_t page = 0; page < pages; page++) {
        volatile uint32_t *words = (volatile uint32_t *) page_addr(page);
        for (uint32_t i = 0; i < PAGE_FRAME_SIZE / sizeof(uint32_t); i++)
            words[i] = swap_pattern(page, i);
    }
    fs_test_check(ctx, "S02", "pages went to swap", swap_manager.free_count < PAGE_FRAME_ON_SWAP_MAX_COUNT,
                  1, swap_manager.free_count < PAGE_FRAME_ON_SWAP_MAX_COUNT);

    for (uint32_t round = 0; round < 2 && bad_page < 0; round++) {
        for (uint32_t page = 0; page < pages && bad_page < 0; page++) {
            volatile uint32_t *words = (volatile uint32_t *) page_addr(page);
            for (uint32_t i = 0; i < PAGE_FRAME_SIZE / sizeof(uint32_t); i++) {
                if (words[i] != swap_pattern(page, i)) {
                    bad_page = (int32_t) page;
                    break;
                }
            }
        }
    }
    fs_test_check(ctx, "S03", "every page still has its data", bad_page == -1, -1, bad_page);

    bool freed = true;
    for (uint32_t page = 0; page < pages; page++)
        freed = paging_free_user_page_frame(page_dir, (void *) page_addr(page)) && freed;
    fs_test_check(ctx, "S04", "every page can be freed", freed, 1, freed);
    fs_test_check(ctx, "S05", "no swap slot leaked", swap_manager.free_count == PAGE_FRAME_ON_SWAP_MAX_COUNT,
                  PAGE_FRAME_ON_SWAP_MAX_COUNT, (int32_t) swap_manager.free_count);
    fs_test_check(ctx, "S06", "no frame leaked", page_manager_state.free_page_frame_count == free_frames_before,
                  (int32_t) free_frames_before, (int32_t) page_manager_state.free_page_frame_count);

    // RAM and swap both full: allocation has to fail, not overwrite a slot
    uint32_t capacity = free_frames_before - 1u + PAGE_FRAME_ON_SWAP_MAX_COUNT;
    uint32_t allocated = 0;
    while (paging_allocate_user_page_frame(page_dir, (void *) page_addr(allocated))) {
        *(volatile uint32_t *) page_addr(allocated) = allocated + 1u;
        allocated++;
    }
    fs_test_check(ctx, "S07", "allocation stops when swap is full", allocated == capacity, (int32_t) capacity, (int32_t) allocated);

    bool full_swap_read = allocated == capacity;
    for (uint32_t page = 0; page < allocated; page++) {
        if (*(volatile uint32_t *) page_addr(page) != page + 1u)
            full_swap_read = false;
    }
    fs_test_check(ctx, "S09", "pages can be restored with both RAM and swap full",
                  full_swap_read, 1, full_swap_read);

    freed = true;
    for (uint32_t page = 0; page < allocated; page++)
        freed = paging_free_user_page_frame(page_dir, (void *) page_addr(page)) && freed;
    fs_test_check(ctx, "S08", "full swap can be emptied", freed && swap_manager.free_count == PAGE_FRAME_ON_SWAP_MAX_COUNT,
                  PAGE_FRAME_ON_SWAP_MAX_COUNT, (int32_t) swap_manager.free_count);
}

// Load a file to address 0 like the kernel loads the shell, it does not fit in low RAM
void mem_suite_load(struct FsTestContext *ctx) {
    pic_remap();
    initialize_idt();
    paging_initialize();
    initialize_filesystem_ext2();

    struct PageDirectory *page_dir = paging_get_current_page_directory_addr();
    volatile uint32_t load_addr = 0;
    uint8_t *source = (uint8_t *) MEM_TEST_BASE;
    uint8_t *target = (uint8_t *) load_addr;
    uint32_t pages = MEM_TEST_LOAD_SIZE / PAGE_FRAME_SIZE;
    uint32_t swap_free_before = swap_manager.free_count;

    for (uint32_t i = 0; i < MEM_TEST_LOAD_SIZE; i++)
        source[i] = fs_test_pattern(i, MEM_TEST_LOAD_SIZE);

    struct EXT2DriverRequest request = {
        .buf          = source,
        .name         = "shell",
        .name_len     = 5,
        .parent_inode = 1,
        .buffer_size  = MEM_TEST_LOAD_SIZE,
        .is_directory = false,
    };
    int8_t retcode = write(&request);
    fs_test_check(ctx, "L01", "file is written", retcode == 0, 0, retcode);

    bool freed = true;
    for (uint32_t page = 0; page < pages; page++)
        freed = paging_free_user_page_frame(page_dir, source + page * PAGE_FRAME_SIZE) && freed;
    fs_test_check(ctx, "L02", "source pages can be freed", freed && swap_manager.free_count == swap_free_before, 1, freed);

    bool needs_swap = pages > page_manager_state.free_page_frame_count;
    request.buf = target;
    retcode = read(request);
    fs_test_check(ctx, "L03", "file is read to address 0", retcode == 0, 0, retcode);
    bool used_swap = swap_manager.free_count < swap_free_before;
    fs_test_check(ctx, "L04", "swap is used only when RAM is too small", used_swap == needs_swap, needs_swap, used_swap);

    int32_t mismatch = fs_test_first_mismatch(target, MEM_TEST_LOAD_SIZE);
    fs_test_check(ctx, "L05", "loaded bytes are correct", mismatch == -1, -1, mismatch);
}
