#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "header/cpu/scheduler.h"
#include "header/cpu/syscall.h"
#include "header/driver/cmos.h"
#include "header/driver/keyboard.h"
#include "header/filesystem/ext2.h"
#include "header/filesystem/index.h"
#include "header/memory/paging.h"
#include "header/memory/swap.h"
#include "header/process/process.h"
#include "header/text/framebuffer.h"
#include "header/stdlib/string.h"

_Static_assert(SYSCALL_PROCESS_NAME_MAX == PROCESS_NAME_LENGTH_MAX,
               "ps name field must match the PCB name length");

static uint8_t console_row;
static uint8_t console_col;

struct SyscallPageRange {
    uint32_t first;
    uint32_t end;
};

static struct SyscallPageRange pinned_ranges[3];
static unsigned pinned_range_count;

static void syscall_unpin_pages(void) {
    struct PageDirectory *page_dir = paging_get_current_page_directory_addr();

    while (pinned_range_count != 0) {
        struct SyscallPageRange range = pinned_ranges[--pinned_range_count];
        for (uint32_t addr = range.first; addr < range.end; addr += PAGE_FRAME_SIZE) {
            struct PageTableEntry *entry = paging_get_page_table_entry(page_dir, (void *)addr);
            entry->ignored &= ~PTE_IGNORED_PINNED;
        }
    }
}

static bool syscall_validate_user_range(const void *address, uint32_t size,
                                        bool writable) {
    uint32_t first;
    uint32_t last;
    struct PageDirectory *page_dir;

    if (size == 0) {
        return true;
    }
    first = (uint32_t)address;
    last = first + size - 1u;
    if (first >= KERNEL_VIRTUAL_ADDRESS_BASE || last < first ||
        last >= KERNEL_VIRTUAL_ADDRESS_BASE) {
        return false;
    }

    page_dir = paging_get_current_page_directory_addr();
    first &= ~(PAGE_FRAME_SIZE - 1u);
    last &= ~(PAGE_FRAME_SIZE - 1u);
    if (pinned_range_count == sizeof(pinned_ranges) / sizeof(pinned_ranges[0]))
        return false;
    struct SyscallPageRange *range = &pinned_ranges[pinned_range_count++];
    *range = (struct SyscallPageRange){first, first};
    while (true) {
        const struct PageDirectoryEntry *directory_entry = &page_dir->table[first >> 22];
        if (!directory_entry->flag.present_bit || !directory_entry->flag.us_bit ||
            directory_entry->flag.use_pagesize_4_mb ||
            (writable && !directory_entry->flag.write_bit)) {
            return false;
        }
        struct PageTableEntry *entry =
            paging_get_page_table_entry(page_dir, (void *)first);
        if (entry == NULL || !entry->flag.us_bit ||
            (writable && !entry->flag.write_bit)) {
            return false;
        }
        if (!entry->flag.present_bit &&
            (!(entry->ignored & PTE_IGNORED_SWAPPED) || !swap_in(page_dir, (void *)first))) {
            return false;
        }
        entry->ignored |= PTE_IGNORED_PINNED;
        range->end = first + PAGE_FRAME_SIZE;
        if (first == last) {
            return true;
        }
        first += PAGE_FRAME_SIZE;
    }
}

static uint8_t console_fg = 0x0F;

static void syscall_write_char(char character) {
    if ((uint8_t)character >= 0x10u && (uint8_t)character <= 0x1Fu) {
        console_fg = (uint8_t)character & 0x0Fu;
        return;
    }
    if (character == '\n') {
        console_col = 0;
        console_row++;
    } else if (character == '\b') {
        if (console_col == 0 && console_row == 0) {
            return;
        }
        if (console_col == 0) {
            console_row--;
            console_col = FRAMEBUFFER_WIDTH;
        }
        console_col--;
        framebuffer_write(console_row, console_col, ' ', 0x0F, 0x00);
    } else if (character >= ' ') {
        framebuffer_write(console_row, console_col, character, console_fg, 0x00);
        console_col++;
    }

    if (console_col >= FRAMEBUFFER_WIDTH) {
        console_col = 0;
        console_row++;
    }
    if (console_row >= FRAMEBUFFER_HEIGHT - 1u) {
        for (uint8_t row = 1; row < FRAMEBUFFER_HEIGHT - 1u; ++row) {
            for (uint8_t col = 0; col < FRAMEBUFFER_WIDTH; ++col) {
                uint32_t source = ((uint32_t)row * FRAMEBUFFER_WIDTH + col) * 2u;
                framebuffer_write(row - 1u, col,
                    FRAMEBUFFER_MEMORY_OFFSET[source],
                    FRAMEBUFFER_MEMORY_OFFSET[source + 1u] & 0x0Fu,
                    FRAMEBUFFER_MEMORY_OFFSET[source + 1u] >> 4);
            }
        }
        for (uint8_t col = 0; col < FRAMEBUFFER_WIDTH; ++col)
            framebuffer_write(FRAMEBUFFER_HEIGHT - 2u, col, ' ', 0x0F, 0x00);
        console_row = FRAMEBUFFER_HEIGHT - 2u;
    }
    framebuffer_set_cursor(console_row, console_col);
}

static int32_t syscall_write_status(const char *buffer, uint32_t size,
                                    uint32_t column) {
    if (column > FRAMEBUFFER_WIDTH || size > FRAMEBUFFER_WIDTH - column ||
        !syscall_validate_user_range(buffer, size, false))
        return -1;

    for (uint32_t i = 0; i < size; ++i) {
        char character = buffer[i];
        if (character == '\n' || character == '\r' || character == '\b')
            character = ' ';
        framebuffer_write(FRAMEBUFFER_HEIGHT - 1u, column + i,
                          character, 0x0F, 0x01);
    }
    return (int32_t)size;
}

static int32_t syscall_write_text(const char *buffer, uint32_t size) {
    if (!syscall_validate_user_range(buffer, size, false)) {
        return -1;
    }
    for (uint32_t index = 0; index < size; index++) {
        syscall_write_char(buffer[index]);
    }
    return (int32_t)size;
}

static int32_t syscall_read_char(char *buffer, uint32_t size) {
    char character;

    if (size == 0 || !syscall_validate_user_range(buffer, 1, true)) {
        return -1;
    }
    get_keyboard_buffer(&character);
    buffer[0] = character;
    return character == '\0' ? 0 : 1;
}

static int32_t syscall_copy_request(uint32_t user_request,
                                    struct EXT2DriverRequest *request,
                                    bool output_buffer) {
    if (!syscall_validate_user_range((void *)user_request, sizeof(*request),
                                     false)) {
        return -1;
    }
    uint8_t directory_flag;
    const uint8_t *source = (const uint8_t *)user_request;
    memcpy(&directory_flag, source + offsetof(struct EXT2DriverRequest, is_directory), 1);
    if (directory_flag > 1) {
        return -1;
    }
    memcpy(request, source, sizeof(*request));
    if (request->name_len == 0 ||
        !syscall_validate_user_range(request->name, request->name_len, false)) {
        return -1;
    }
    if (request->buffer_size != 0 &&
        !syscall_validate_user_range(request->buf, request->buffer_size,
                                     output_buffer)) {
        return -1;
    }
    return 0;
}

/* The caller's address space is freed from the kernel directory, then the next process runs. */
__attribute__((noreturn))
static void syscall_exit_current(int32_t code) {
    struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();
    uint32_t pid = current->metadata.pid;

    if (pid == PROCESS_INIT_PID) {
        kernel_halt("tapOS: init exited");
    }
    syscall_unpin_pages();
    process_terminate(pid, code);
    paging_use_page_directory(&_paging_kernel_page_directory);
    process_reap(pid);
    scheduler_switch_to_next_process();
}

static int32_t syscall_kill(uint32_t pid) {
    struct ProcessControlBlock *target;

    if (pid == PROCESS_INIT_PID) {
        return 2;
    }
    target = process_lookup(pid);
    if (target == NULL || (target->metadata.state != PROCESS_READY &&
                           target->metadata.state != PROCESS_RUNNING)) {
        return 1;
    }
    if (target == process_get_current_running_pcb_pointer()) {
        syscall_exit_current(0);
    }
    return process_destroy(pid) ? 0 : 1;
}

static int32_t syscall_process_list(struct SyscallProcessInfo *out, uint32_t capacity) {
    uint32_t count = 0;

    if (capacity > PROCESS_COUNT_MAX) {
        capacity = PROCESS_COUNT_MAX;
    }
    if (!syscall_validate_user_range(out, capacity * sizeof(*out), true)) {
        return -1;
    }
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX && count < capacity; i++) {
        const struct ProcessControlBlock *pcb = &_process_list[i];
        if (pcb->metadata.state != PROCESS_READY && pcb->metadata.state != PROCESS_RUNNING) {
            continue;
        }
        struct SyscallProcessInfo info = {
            .pid = pcb->metadata.pid,
            .state = (uint32_t)pcb->metadata.state,
        };
        memcpy(info.name, pcb->metadata.name, SYSCALL_PROCESS_NAME_MAX - 1u);
        memcpy(&out[count++], &info, sizeof(info));
    }
    return (int32_t)count;
}

void syscall_dispatch(struct InterruptFrame *frame) {
    struct EXT2DriverRequest request;
    uint32_t service;
    uint32_t argument;
    int32_t result = -1;

    if (frame == NULL) {
        return;
    }
    service = frame->cpu.general.eax;
    argument = frame->cpu.general.ebx;

    switch (service) {
        case SYSCALL_WRITE_TEXT:
            result = syscall_write_text((const char *)argument,
                                        frame->cpu.general.ecx);
            break;
        case SYSCALL_READ_CHAR:
            result = syscall_read_char((char *)argument,
                                       frame->cpu.general.ecx);
            break;
        case SYSCALL_WRITE_STATUS:
            result = syscall_write_status((const char *)argument,
                                          frame->cpu.general.ecx,
                                          frame->cpu.general.edx);
            break;
        case SYSCALL_READ_FILE:
            if (syscall_copy_request(argument, &request, true) == 0) {
                result = read(request);
            }
            break;
        case SYSCALL_READ_DIRECTORY:
            if (syscall_copy_request(argument, &request, true) == 0) {
                result = read_directory(&request);
            }
            break;
        case SYSCALL_WRITE_FILE:
            if (syscall_copy_request(argument, &request, false) == 0) {
                result = write(&request);
            }
            break;
        case SYSCALL_DELETE_FILE:
            if (syscall_copy_request(argument, &request, false) == 0) {
                result = delete(request);
            }
            break;
        case SYSCALL_EXEC: {
            uint32_t *user_pid = (void *)frame->cpu.general.ecx;
            uint32_t pid;
            if (argument == 0 || user_pid == NULL ||
                syscall_copy_request(argument, &request, false) != 0 ||
                request.buffer_size != 0 ||
                !syscall_validate_user_range(user_pid, sizeof(*user_pid), true)) {
                result = PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT;
                break;
            }
            result = process_create_user_process(request, &pid);
            if (result == PROCESS_CREATE_SUCCESS)
                *user_pid = pid;
            break;
        }
        case SYSCALL_EXIT:
            syscall_exit_current((int32_t)argument);
        case SYSCALL_KILL:
            result = syscall_kill(argument);
            break;
        case SYSCALL_PROCESS_LIST:
            result = syscall_process_list((struct SyscallProcessInfo *)argument,
                                          frame->cpu.general.ecx);
            break;
        case SYSCALL_GET_TIME:
            if (syscall_validate_user_range((void *)argument, sizeof(struct CMOSTime), true)) {
                struct CMOSTime now;
                cmos_read_time(&now);
                memcpy((void *)argument, &now, sizeof(now));
                result = 0;
            }
            break;
        case SYSCALL_READ_DIRECTORY_BLOCK:
            if (syscall_validate_user_range((void *)frame->cpu.general.edx, BLOCK_SIZE, true))
                result = ext2_read_directory_block(argument, frame->cpu.general.ecx,
                                                   (void *)frame->cpu.general.edx);
            break;
        case SYSCALL_MOVE: {
            struct EXT2MoveRequest move;
            if (!syscall_validate_user_range((void *)argument, sizeof(move), false))
                break;
            memcpy(&move, (void *)argument, sizeof(move));
            if (syscall_validate_user_range(move.source_name, move.source_length, false) &&
                syscall_validate_user_range(move.destination_name, move.destination_length, false))
                result = ext2_move(&move);
            break;
        }
        case SYSCALL_FIND: {
            struct SyscallFindRequest find;
            if (!syscall_validate_user_range((void *)argument, sizeof(find), true))
                break;
            memcpy(&find, (void *)argument, sizeof(find));
            if (find.name_len == 0 || !syscall_validate_user_range(find.name, find.name_len, false) ||
                !syscall_validate_user_range(find.out, find.out_size, true))
                break;
            uint32_t written = 0;
            result = fs_index_find(find.name, find.name_len, find.out, find.out_size, &written);
            find.written = written;
            ((struct SyscallFindRequest *)argument)->written = written;
            break;
        }
        default:
            break;
    }
    syscall_unpin_pages();
    frame->cpu.general.eax = (uint32_t)result;
    process_save_interrupt_context(frame);
}
