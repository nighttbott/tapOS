#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/idt.h"
#include "header/cpu/interrupt.h"
#include "header/filesystem/ext2.h"
#include "header/memory/paging.h"
#include "header/memory/process_memory.h"
#include "header/memory/swap.h"
#include "fs_test.h"
#include "mem_cases.h"

#define MEM_TEST_BASE 0x00100000u
#define MEM_TEST_LOAD_SIZE 0x100000u
#define MEM_TEST_SWAP_PAGES (PAGE_FRAME_MAX_COUNT - KERNEL_RESERVED_PAGE_FRAME_COUNT + 64u)


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

        bool replaced = paging_allocate_user_page_frame(page_dir, (void *) page_addr(pages));
        fs_test_check(ctx, "M05", "full memory replaces a page", replaced, 1, replaced);

        bool freed = true;
        for (uint32_t page = 0; page <= pages; page++)
            freed = paging_free_user_page_frame(page_dir, (void *) page_addr(page)) && freed;
        fs_test_check(ctx, "M06", "every page can be freed", freed, 1, freed);
        fs_test_check(ctx, "M07", "no frame leaked", page_manager_state.free_page_frame_count == free_frames_before - 1u,
                      (int32_t) free_frames_before - 1, (int32_t) page_manager_state.free_page_frame_count);
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
    // One frame stays with the page table of this test
    fs_test_check(ctx, "S06", "no frame leaked", page_manager_state.free_page_frame_count == free_frames_before - 1u,
                  (int32_t) free_frames_before - 1, (int32_t) page_manager_state.free_page_frame_count);

    // RAM and swap both full: allocation has to fail, not overwrite a slot
    uint32_t capacity = free_frames_before - 1u + PAGE_FRAME_ON_SWAP_MAX_COUNT;
    uint32_t allocated = 0;
    while (paging_allocate_user_page_frame(page_dir, (void *) page_addr(allocated)))
        allocated++;
    fs_test_check(ctx, "S07", "allocation stops when swap is full", allocated == capacity, (int32_t) capacity, (int32_t) allocated);

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

#define MEM_TEST_PROGRAM_SIZE 10000u  // file size: two full pages and a part of the third
#define MEM_TEST_IMAGE_SIZE   0x5000u // image with .bss: five pages

static uint8_t program[MEM_TEST_PROGRAM_SIZE];

static struct EXT2DriverRequest program_request(char *name, uint32_t buf, uint32_t size) {
    return (struct EXT2DriverRequest) {
        .buf          = (void *) buf,
        .name         = name,
        .name_len     = (uint8_t) (name[0] == 'p' ? 4 : 7),
        .parent_inode = 1,
        .buffer_size  = size,
        .is_directory = false,
    };
}

void mem_suite_process(struct FsTestContext *ctx) {
    pic_remap();
    initialize_idt();
    paging_initialize();
    initialize_filesystem_ext2();

    struct PageDirectory *kernel_dir = paging_get_current_page_directory_addr();
    struct ProcessMemory first;
    struct ProcessMemory second;
    struct ProcessMemory failed;
    char name[] = "prog";
    char missing[] = "missing";

    for (uint32_t i = 0; i < MEM_TEST_PROGRAM_SIZE; i++)
        program[i] = fs_test_pattern(i, MEM_TEST_PROGRAM_SIZE);
    struct EXT2DriverRequest file = program_request(name, (uint32_t) program, MEM_TEST_PROGRAM_SIZE);
    fs_test_check(ctx, "P00", "program is written", write(&file) == 0, 0, 0);

    uint32_t free_before = page_manager_state.free_page_frame_count;
    uint32_t swap_before = swap_manager.free_count;

    int32_t code = process_memory_load(&first, program_request(name, 0, MEM_TEST_IMAGE_SIZE));
    fs_test_check(ctx, "P01", "first program loads", code == PROCESS_LOAD_SUCCESS, 0, code);
    code = process_memory_load(&second, program_request(name, 0, MEM_TEST_IMAGE_SIZE));
    fs_test_check(ctx, "P01", "second program loads", code == PROCESS_LOAD_SUCCESS, 0, code);
    fs_test_check(ctx, "P01", "each has its own directory",
                  first.page_dir != NULL && second.page_dir != NULL && first.page_dir != second.page_dir &&
                  first.page_dir != kernel_dir, 1, 0);
    fs_test_check(ctx, "P01", "caller directory is active again",
                  paging_get_current_page_directory_addr() == kernel_dir, 1, 0);
    fs_test_check(ctx, "P01", "entrypoint, stack and page counts",
                  first.entrypoint == 0 && first.stack_top == PROCESS_STACK_TOP &&
                  first.image_pages == 5 && first.stack_pages == PROCESS_STACK_PAGES, 1, 0);
    uint32_t per_process = 5 + PROCESS_STACK_PAGES + 2;  // image, stack, two page tables
    fs_test_check(ctx, "P01", "frames taken: image, stack and page tables",
                  page_manager_state.free_page_frame_count == free_before - 2 * per_process,
                  (int32_t) (free_before - 2 * per_process), (int32_t) page_manager_state.free_page_frame_count);

    // Inside the first address space
    paging_enter_page_directory(first.page_dir);
    int32_t mismatch = fs_test_first_mismatch((uint8_t *) 0, MEM_TEST_PROGRAM_SIZE);
    bool bss_zero = true;
    for (uint32_t addr = MEM_TEST_PROGRAM_SIZE; addr < MEM_TEST_IMAGE_SIZE; addr++)
        bss_zero = bss_zero && *(volatile uint8_t *) addr == 0;
    volatile uint32_t *stack_word = (volatile uint32_t *) (PROCESS_STACK_TOP - 4);
    *stack_word = 0x11111111u;
    *(volatile uint8_t *) 0 = 0xAA;
    paging_use_page_directory(kernel_dir);
    fs_test_check(ctx, "P02", "image bytes are the file", mismatch == -1, -1, mismatch);
    fs_test_check(ctx, "P02", ".bss after the file is zero", bss_zero, 1, bss_zero);

    paging_enter_page_directory(second.page_dir);
    uint8_t second_first_byte = *(volatile uint8_t *) 0;
    uint32_t second_stack = *(volatile uint32_t *) (PROCESS_STACK_TOP - 4);
    paging_use_page_directory(kernel_dir);
    fs_test_check(ctx, "P03", "code page is not shared", second_first_byte == fs_test_pattern(0, MEM_TEST_PROGRAM_SIZE),
                  fs_test_pattern(0, MEM_TEST_PROGRAM_SIZE), second_first_byte);
    fs_test_check(ctx, "P03", "stack is not shared", second_stack == 0, 0, (int32_t) second_stack);

    code = process_memory_load(&failed, program_request(missing, 0, MEM_TEST_IMAGE_SIZE));
    fs_test_check(ctx, "P04", "missing file", code == PROCESS_LOAD_FAIL_FS_READ_FAILURE, 4, code);
    code = process_memory_load(&failed, program_request(name, 0, 0x1000));
    fs_test_check(ctx, "P04", "image smaller than the file", code == PROCESS_LOAD_FAIL_FS_READ_FAILURE, 4, code);
    code = process_memory_load(&failed, program_request(name, KERNEL_VIRTUAL_ADDRESS_BASE, 0x1000));
    fs_test_check(ctx, "P04", "entrypoint in the kernel", code == PROCESS_LOAD_FAIL_INVALID_ENTRYPOINT, 2, code);
    code = process_memory_load(&failed, program_request(name, 0, PROCESS_IMAGE_SIZE_MAX + 1));
    fs_test_check(ctx, "P04", "image too large", code == PROCESS_LOAD_FAIL_INVALID_ENTRYPOINT, 2, code);
    code = process_memory_load(&failed, program_request(name, PROCESS_STACK_TOP - PROCESS_STACK_SIZE - 0x1000, 0x2000));
    fs_test_check(ctx, "P04", "image over the stack", code == PROCESS_LOAD_FAIL_INVALID_ENTRYPOINT, 2, code);
    fs_test_check(ctx, "P04", "failures leave nothing behind",
                  failed.page_dir == NULL && page_manager_state.free_page_frame_count == free_before - 2 * per_process,
                  (int32_t) (free_before - 2 * per_process), (int32_t) page_manager_state.free_page_frame_count);

    // Release the second one while it is active, like exit would
    paging_enter_page_directory(second.page_dir);
    bool released = process_memory_release(&second);
    fs_test_check(ctx, "P05", "release of the active directory",
                  released && paging_get_current_page_directory_addr() == &_paging_kernel_page_directory, 1, released);
    released = process_memory_release(&first);
    fs_test_check(ctx, "P05", "release of an inactive directory", released, 1, released);
    fs_test_check(ctx, "P05", "every frame is back", page_manager_state.free_page_frame_count == free_before,
                  (int32_t) free_before, (int32_t) page_manager_state.free_page_frame_count);
    fs_test_check(ctx, "P05", "every swap slot is back", swap_manager.free_count == swap_before,
                  (int32_t) swap_before, (int32_t) swap_manager.free_count);
    fs_test_check(ctx, "P05", "release twice is refused", !process_memory_release(&first), 1, 0);

    struct PageDirectory *dirs[PAGING_DIRECTORY_TABLE_MAX_COUNT + 1];
    uint32_t made = 0;
    while (made <= PAGING_DIRECTORY_TABLE_MAX_COUNT && (dirs[made] = paging_create_new_page_directory()) != NULL)
        made++;
    fs_test_check(ctx, "P06", "pool holds 32 directories", made == PAGING_DIRECTORY_TABLE_MAX_COUNT,
                  PAGING_DIRECTORY_TABLE_MAX_COUNT, (int32_t) made);
    code = process_memory_load(&failed, program_request(name, 0, MEM_TEST_IMAGE_SIZE));
    fs_test_check(ctx, "P06", "load with an empty pool", code == PROCESS_LOAD_FAIL_NO_PAGE_DIRECTORY, 1, code);
    bool all_freed = true;
    for (uint32_t i = 0; i < made; i++)
        all_freed = paging_free_page_directory(dirs[i]) && all_freed;
    fs_test_check(ctx, "P06", "pool can be emptied", all_freed, 1, all_freed);
    fs_test_check(ctx, "P06", "kernel directory is not in the pool", !paging_free_page_directory(kernel_dir), 1, 0);
    fs_test_check(ctx, "P06", "no frame leaked", page_manager_state.free_page_frame_count == free_before,
                  (int32_t) free_before, (int32_t) page_manager_state.free_page_frame_count);

    // Two 1 MiB images. On 6 MiB of RAM the second one only fits when pages of the first,
    // inactive process go to swap; they must come back intact on access.
    uint32_t big_pages = PROCESS_IMAGE_SIZE_MAX / PAGE_FRAME_SIZE;
    code = process_memory_load(&first, program_request(name, 0, PROCESS_IMAGE_SIZE_MAX));
    fs_test_check(ctx, "P07", "first 1 MiB image", code == PROCESS_LOAD_SUCCESS, 0, code);
    paging_enter_page_directory(first.page_dir);
    for (uint32_t page = 3; page < big_pages; page++)
        *(volatile uint32_t *) (page * PAGE_FRAME_SIZE) = 0xB0000000u + page;
    paging_use_page_directory(kernel_dir);

    // RAM and swap together must hold both images, otherwise the loader has to refuse
    uint32_t big_frames = big_pages + PROCESS_STACK_PAGES + 2u;
    bool fits = 2u * big_frames <= free_before + swap_before;
    code = process_memory_load(&second, program_request(name, 0, PROCESS_IMAGE_SIZE_MAX));
    fs_test_check(ctx, "P07", fits ? "second 1 MiB image" : "second image refused: RAM and swap too small",
                  code == (fits ? PROCESS_LOAD_SUCCESS : PROCESS_LOAD_FAIL_NOT_ENOUGH_MEMORY),
                  fits ? PROCESS_LOAD_SUCCESS : PROCESS_LOAD_FAIL_NOT_ENOUGH_MEMORY, code);
    bool needed_swap = 2u * big_frames > free_before;
    bool used_swap = swap_manager.free_count < swap_before;
    if (fits)
        fs_test_check(ctx, "P07", "swap used only when RAM is too small", used_swap == needed_swap,
                      needed_swap, used_swap);

    paging_enter_page_directory(first.page_dir);
    bool intact = fs_test_first_mismatch((uint8_t *) 0, MEM_TEST_PROGRAM_SIZE) == -1;
    for (uint32_t page = 3; page < big_pages; page++)
        intact = intact && *(volatile uint32_t *) (page * PAGE_FRAME_SIZE) == 0xB0000000u + page;
    paging_use_page_directory(kernel_dir);
    fs_test_check(ctx, "P07", "first process pages survive swap", intact, 1, intact);

    bool both = process_memory_release(&first) && (!fits || process_memory_release(&second));
    fs_test_check(ctx, "P07", "both released", both, 1, both);
    fs_test_check(ctx, "P07", "frames and swap slots are back",
                  page_manager_state.free_page_frame_count == free_before && swap_manager.free_count == swap_before,
                  (int32_t) free_before, (int32_t) page_manager_state.free_page_frame_count);
}
