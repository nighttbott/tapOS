#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/memory/process_memory.h"
#include "header/memory/paging.h"
#include "header/memory/swap.h"
#include "header/stdlib/string.h"

static uint32_t page_down(uint32_t addr) {
    return addr & ~(PAGE_FRAME_SIZE - 1u);
}

/* Pages from start up to end, both page aligned. */
static uint32_t page_count(uint32_t start, uint32_t end) {
    return (end - start) / PAGE_FRAME_SIZE;
}

/* Maps zeroed user pages over [start, end) of the active directory. */
static bool map_range(struct PageDirectory *page_dir, uint32_t start, uint32_t end) {
    for (uint32_t addr = start; addr < end; addr += PAGE_FRAME_SIZE)
        if (!paging_allocate_user_page_frame(page_dir, (void *) addr))
            return false;
    return true;
}

int32_t process_memory_load(struct ProcessMemory *memory, struct EXT2DriverRequest request) {
    static char name[256];
    uint32_t start = (uint32_t) request.buf;
    uint32_t stack_bottom = PROCESS_STACK_TOP - PROCESS_STACK_SIZE;
    uint32_t image_start;
    uint32_t image_end;
    uint32_t frames_needed;
    uint32_t page_tables;
    struct PageDirectory *page_dir;
    struct PageDirectory *previous;
    int8_t read_code;

    memset(memory, 0, sizeof(*memory));

    // Entrypoint in user space, image under the stack. Written as subtractions so nothing overflows.
    if (start >= stack_bottom || request.buffer_size == 0 ||
        request.buffer_size > PROCESS_IMAGE_SIZE_MAX ||
        request.buffer_size > stack_bottom - start)
        return PROCESS_LOAD_FAIL_INVALID_ENTRYPOINT;

    image_start = page_down(start);
    image_end = page_down(start + request.buffer_size + PAGE_FRAME_SIZE - 1u);

    // Count frames, not bytes: image and stack pages plus one page table per 4 MiB they touch
    page_tables = ((image_end - 1u) >> 22) - (image_start >> 22) + 1u;
    if ((stack_bottom >> 22) != ((image_end - 1u) >> 22))
        page_tables++;
    frames_needed = page_count(image_start, image_end) + PROCESS_STACK_PAGES + page_tables;
    // Pages of other processes can go to swap to make room
    if (frames_needed > page_manager_state.free_page_frame_count + swap_manager.free_count)
        return PROCESS_LOAD_FAIL_NOT_ENOUGH_MEMORY;

    // The name may live in the caller's user memory, which the new directory does not map
    memcpy(name, request.name, request.name_len);
    request.name = name;

    page_dir = paging_create_new_page_directory();
    if (page_dir == NULL)
        return PROCESS_LOAD_FAIL_NO_PAGE_DIRECTORY;

    // Book 4.1.3.2: switch to the new address space only while it is being filled
    previous = paging_enter_page_directory(page_dir);
    if (!map_range(page_dir, image_start, image_end) ||
        !map_range(page_dir, stack_bottom, PROCESS_STACK_TOP)) {
        paging_use_page_directory(previous);
        paging_free_page_directory(page_dir);
        return PROCESS_LOAD_FAIL_NOT_ENOUGH_MEMORY;
    }
    read_code = read(request);
    paging_use_page_directory(previous);

    // A process with a broken image must never run
    if (read_code != 0) {
        paging_free_page_directory(page_dir);
        return PROCESS_LOAD_FAIL_FS_READ_FAILURE;
    }

    memory->page_dir = page_dir;
    memory->entrypoint = start;
    memory->stack_top = PROCESS_STACK_TOP;
    memory->image_pages = page_count(image_start, image_end);
    memory->stack_pages = PROCESS_STACK_PAGES;
    return PROCESS_LOAD_SUCCESS;
}

bool process_memory_release(struct ProcessMemory *memory) {
    if (memory->page_dir == NULL || !paging_free_page_directory(memory->page_dir))
        return false;
    memset(memory, 0, sizeof(*memory));
    return true;
}
