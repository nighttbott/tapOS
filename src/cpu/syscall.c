#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "header/cpu/syscall.h"
#include "header/driver/keyboard.h"
#include "header/filesystem/ext2.h"
#include "header/memory/paging.h"
#include "header/text/framebuffer.h"
#include "header/stdlib/string.h"

static uint8_t console_row;
static uint8_t console_col;

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
    while (true) {
        const struct PageDirectoryEntry *directory_entry = &page_dir->table[first >> 22];
        if (!directory_entry->flag.present_bit || !directory_entry->flag.us_bit ||
            directory_entry->flag.use_pagesize_4_mb ||
            (writable && !directory_entry->flag.write_bit)) {
            return false;
        }
        struct PageTableEntry *entry =
            paging_get_page_table_entry(page_dir, (void *)first);
        if (entry == NULL || !entry->flag.present_bit || !entry->flag.us_bit ||
            (writable && !entry->flag.write_bit)) {
            return false;
        }
        if (first == last) {
            return true;
        }
        first += PAGE_FRAME_SIZE;
    }
}

static void syscall_write_char(char character) {
    if (character == '\n') {
        console_col = 0;
        console_row++;
    } else if (character == '\b') {
        if (console_col > 0) {
            console_col--;
        }
        framebuffer_write(console_row, console_col, ' ', 0x0F, 0x00);
    } else if (character >= ' ') {
        framebuffer_write(console_row, console_col, character, 0x0F, 0x00);
        console_col++;
    }

    if (console_col >= FRAMEBUFFER_WIDTH) {
        console_col = 0;
        console_row++;
    }
    if (console_row >= FRAMEBUFFER_HEIGHT) {
        framebuffer_scroll_up();
        console_row = FRAMEBUFFER_HEIGHT - 1u;
    }
    framebuffer_set_cursor(console_row, console_col);
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
        default:
            break;
    }
    frame->cpu.general.eax = (uint32_t)result;
}
