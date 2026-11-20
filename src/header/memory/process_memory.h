#ifndef _PROCESS_MEMORY_H
#define _PROCESS_MEMORY_H

#include <stdint.h>
#include <stdbool.h>
#include "header/memory/paging.h"
#include "header/filesystem/ext2.h"

/*
 * Address space of one user process (Book 4.1.3.1 and 4.1.3.2), owned by member B.
 * The process table (member A) keeps one ProcessMemory per PCB, calls process_memory_load
 * from process_create_user_process and process_memory_release from process_destroy.
 *
 * Layout:  [request.buf, request.buf + request.buffer_size)   code, data and .bss
 *          [PROCESS_STACK_TOP - PROCESS_STACK_SIZE, PROCESS_STACK_TOP)   user stack
 * Every page is mapped and zeroed up front, because syscalls accept only present pages.
 */

// Book 3.4.2.2: the shell is assumed to be smaller than 1 MiB
#define PROCESS_IMAGE_SIZE_MAX 0x100000u
#define PROCESS_STACK_PAGES    4u
#define PROCESS_STACK_SIZE     (PROCESS_STACK_PAGES * PAGE_FRAME_SIZE)
#define PROCESS_STACK_TOP      KERNEL_VIRTUAL_ADDRESS_BASE

// Same values as PROCESS_CREATE_* in the kit's process.h, so A can return them unchanged
#define PROCESS_LOAD_SUCCESS                   0
#define PROCESS_LOAD_FAIL_NO_PAGE_DIRECTORY    1  // pool of PAGING_DIRECTORY_TABLE_MAX_COUNT used up
#define PROCESS_LOAD_FAIL_INVALID_ENTRYPOINT   2
#define PROCESS_LOAD_FAIL_NOT_ENOUGH_MEMORY    3
#define PROCESS_LOAD_FAIL_FS_READ_FAILURE      4

/**
 * Memory of one process, kept in its PCB.
 *
 * @param page_dir     Page directory of the process, the value for CR3 / Context
 * @param entrypoint   Initial EIP: request.buf
 * @param stack_top    Initial ESP: PROCESS_STACK_TOP
 * @param image_pages  Pages mapped for the executable image
 * @param stack_pages  Pages mapped for the stack
 */
struct ProcessMemory {
    struct PageDirectory *page_dir;
    uint32_t              entrypoint;
    uint32_t              stack_top;
    uint32_t              image_pages;
    uint32_t              stack_pages;
};

/**
 * Creates a new address space and loads an executable into it.
 * request.buf is where the program starts (0 for the user linker script) and
 * request.buffer_size is the size of the image to map, .bss included. The file must fit in it.
 * The active page directory is the same before and after the call.
 *
 * @param memory  Filled on success, cleared on failure
 * @param request Read request of the executable; name may point into the caller's user memory
 * @return        PROCESS_LOAD_* code. On failure nothing stays allocated
 */
int32_t process_memory_load(struct ProcessMemory *memory, struct EXT2DriverRequest request);

/**
 * Frees everything of the process: its pages, its pages in swap, its page tables and its page
 * directory. Safe while that directory is active: the kernel directory is loaded instead, so the
 * caller must switch to another process before returning to user mode.
 *
 * @return False when memory holds no address space
 */
bool process_memory_release(struct ProcessMemory *memory);

#endif
