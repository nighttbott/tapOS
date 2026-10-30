#include <stdint.h>
#include <stdbool.h>
#include "header/memory/paging.h"
#include "fs_test.h"
#include "mem_cases.h"

#define MEM_TEST_BASE 0x00100000u

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

        fs_test_check(ctx, "M05", "full memory is refused", !paging_allocate_user_page_frame(page_dir, (void *) page_addr(pages)), 1, 1);

        bool freed = true;
        for (uint32_t page = 0; page < pages; page++)
            freed = paging_free_user_page_frame(page_dir, (void *) page_addr(page)) && freed;
        fs_test_check(ctx, "M06", "every page can be freed", freed, 1, freed);
        fs_test_check(ctx, "M07", "no frame leaked", page_manager_state.free_page_frame_count == free_frames_before - 1u,
                      (int32_t) free_frames_before - 1, (int32_t) page_manager_state.free_page_frame_count);
    }

    fs_test_check(ctx, "M08", "kernel address is refused", !paging_allocate_user_page_frame(page_dir, (void *) KERNEL_VIRTUAL_ADDRESS_BASE), 1, 1);
    fs_test_check(ctx, "M09", "freeing twice is refused", !paging_free_user_page_frame(page_dir, (void *) page_addr(0)), 1, 1);
}
