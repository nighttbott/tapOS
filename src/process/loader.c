#include "header/process/process.h"
#include "header/cpu/gdt.h"
#include "header/stdlib/string.h"

#define INIT_IMAGE_SIZE 0x40000u
#define INIT_STACK_PAGES 4u
#define USER_IMAGE_PAGES (PROCESS_PAGE_FRAME_COUNT_MAX - 1u)
#define ELF_LOAD 1u
#define ELF_EXECUTE 1u
#define ELF_WRITE 2u
#define ELF_MAX_SEGMENTS 2u

struct ElfHeader {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version, entry, program_offset, section_offset, flags;
    uint16_t header_size, program_size, program_count;
    uint16_t section_size, section_count, section_names;
};

struct ElfSegment {
    uint32_t type, offset, address, physical_address, file_size, memory_size, flags, alignment;
};

_Static_assert(sizeof(struct ElfHeader) == 52, "ELF32 header size");
_Static_assert(sizeof(struct ElfSegment) == 32, "ELF32 program header size");
_Static_assert(USER_IMAGE_PAGES < 32, "Image page mask width");

/* The syscall interrupt gate serializes loads on the single CPU. */
static uint8_t executable[2u * PROCESS_PAGE_FRAME_COUNT_MAX * PAGE_FRAME_SIZE];

static bool parse_executable(uint32_t size, struct ElfSegment *segments, uint32_t *pages) {
    static const uint8_t signature[] = {0x7F, 'E', 'L', 'F', 1, 1, 1};
    struct ElfHeader header;
    bool has_entry = false;
    *pages = 0;

    if (size < sizeof(header) || memcmp(executable, signature, sizeof(signature)) != 0)
        return false;
    memcpy(&header, executable, sizeof(header));
    if (header.type != 2 || header.machine != 3 || header.version != 1 || header.entry != 0 ||
        header.header_size != sizeof(header) || header.program_size != sizeof(*segments) ||
        header.program_count == 0 || header.program_count > ELF_MAX_SEGMENTS ||
        header.program_offset < sizeof(header) || header.program_offset > size ||
        header.program_count * sizeof(*segments) > size - header.program_offset)
        return false;
    memcpy(segments, executable + header.program_offset, header.program_count * sizeof(*segments));

    for (uint32_t i = 0; i < header.program_count; i++) {
        struct ElfSegment *segment = &segments[i];
        if (segment->type != ELF_LOAD || segment->file_size > segment->memory_size)
            return false;
        if (segment->memory_size == 0)
            continue;

        uint32_t limit = USER_IMAGE_PAGES * PAGE_FRAME_SIZE;
        if (segment->address >= limit || segment->memory_size > limit - segment->address ||
            segment->offset > size || segment->file_size > size - segment->offset ||
            segment->address % PAGE_FRAME_SIZE != 0 || segment->offset % PAGE_FRAME_SIZE != 0)
            return false;

        uint32_t first = segment->address / PAGE_FRAME_SIZE;
        uint32_t count = (segment->memory_size + PAGE_FRAME_SIZE - 1u) / PAGE_FRAME_SIZE;
        uint32_t mask = ((1u << count) - 1u) << first;
        if (*pages & mask)
            return false;
        *pages |= mask;
        if (segment->address == header.entry && segment->file_size != 0 && (segment->flags & ELF_EXECUTE))
            has_entry = true;
    }
    return has_entry;
}

static bool map_stack(struct PageDirectory *page_dir, uint32_t pages) {
    for (uint32_t i = 1; i <= pages; i++) {
        void *address = (void *)(KERNEL_VIRTUAL_ADDRESS_BASE - i * PAGE_FRAME_SIZE);
        if (!paging_allocate_user_page_frame(page_dir, address))
            return false;
    }
    return true;
}

static int32_t load_elf(struct PageDirectory *page_dir, struct EXT2DriverRequest request) {
    struct ElfSegment segments[ELF_MAX_SEGMENTS] = {0};
    uint32_t size, pages;
    request.buf = executable;
    request.buffer_size = sizeof(executable);

    int8_t status = ext2_read_file(request, &size);
    if (status != 0)
        return status == 2 ? PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT : PROCESS_CREATE_FAIL_FS_READ_FAILURE;
    if (!parse_executable(size, segments, &pages))
        return PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT;

    uint32_t frame_count = 3;  /* One stack frame and two user page tables. */
    for (uint32_t mask = pages; mask != 0; mask >>= 1)
        frame_count += mask & 1u;
    if (!paging_allocate_check(frame_count * PAGE_FRAME_SIZE))
        return PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;

    paging_use_page_directory(page_dir);
    for (uint32_t i = 0; i < USER_IMAGE_PAGES; i++) {
        if ((pages & (1u << i)) &&
            !paging_allocate_user_page_frame(page_dir, (void *)(i * PAGE_FRAME_SIZE)))
            return PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
    }
    if (!map_stack(page_dir, 1))
        return PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;

    for (uint32_t i = 0; i < ELF_MAX_SEGMENTS; i++) {
        const struct ElfSegment *segment = &segments[i];
        if (segment->file_size != 0)
            memcpy((void *)segment->address, executable + segment->offset, segment->file_size);

        if (!(segment->flags & ELF_WRITE)) {
            uint32_t end = segment->address + segment->memory_size;
            for (uint32_t addr = segment->address; addr < end; addr += PAGE_FRAME_SIZE) {
                struct PageTableEntry *entry = paging_get_page_table_entry(page_dir, (void *)addr);
                entry->flag.write_bit = false;
                flush_single_tlb((void *)addr);
            }
        }
    }
    return PROCESS_CREATE_SUCCESS;
}

static int32_t load_flat_init(struct PageDirectory *page_dir, struct EXT2DriverRequest request) {
    if (!paging_allocate_check(INIT_IMAGE_SIZE + (INIT_STACK_PAGES + 2u) * PAGE_FRAME_SIZE))
        return PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;

    paging_use_page_directory(page_dir);
    for (uint32_t addr = 0; addr < INIT_IMAGE_SIZE; addr += PAGE_FRAME_SIZE) {
        if (!paging_allocate_user_page_frame(page_dir, (void *)addr))
            return PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
    }
    if (!map_stack(page_dir, INIT_STACK_PAGES))
        return PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;

    uint32_t size;
    request.buffer_size = INIT_IMAGE_SIZE;
    int8_t status = ext2_read_file(request, &size);
    if (status == 2 || (status == 0 && size == 0))
        return PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT;
    return status == 0 ? PROCESS_CREATE_SUCCESS : PROCESS_CREATE_FAIL_FS_READ_FAILURE;
}

static int32_t create_process(struct EXT2DriverRequest request, uint32_t *pid_out, bool init) {
    if (request.buf != NULL || request.is_directory || request.name == NULL ||
        request.name_len == 0 || request.name_len > PROCESS_NAME_LENGTH_MAX)
        return PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT;

    struct ProcessControlBlock *pcb = process_reserve(request.name, request.name_len, init);
    if (pcb == NULL)
        return PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;

    request.name = pcb->metadata.name;
    struct PageDirectory *previous = paging_get_current_page_directory_addr();
    struct PageDirectory *page_dir = paging_create_new_page_directory();
    int32_t result = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
    if (page_dir != NULL) {
        result = init ? load_flat_init(page_dir, request) : load_elf(page_dir, request);
        paging_use_page_directory(previous);
    }
    if (result != PROCESS_CREATE_SUCCESS) {
        if (page_dir != NULL)
            paging_free_page_directory(page_dir);
        process_release(pcb);
        return result;
    }

    pcb->context = (struct Context) {
        .cpu = {
            .stack.esp = KERNEL_VIRTUAL_ADDRESS_BASE,
            .segment = {
                .ds = GDT_USER_DATA_SEGMENT_SELECTOR, .es = GDT_USER_DATA_SEGMENT_SELECTOR,
                .fs = GDT_USER_DATA_SEGMENT_SELECTOR, .gs = GDT_USER_DATA_SEGMENT_SELECTOR,
            },
        },
        .cs = GDT_USER_CODE_SEGMENT_SELECTOR,
        .ss = GDT_USER_DATA_SEGMENT_SELECTOR,
        .eflags = CPU_EFLAGS_BASE_FLAG | CPU_EFLAGS_FLAG_INTERRUPT_ENABLE,
        .page_directory_virtual_addr = page_dir,
    };
    process_mark_ready(pcb);
    if (pid_out != NULL)
        *pid_out = pcb->metadata.pid;
    return PROCESS_CREATE_SUCCESS;
}

int32_t process_create_user_process(struct EXT2DriverRequest request, uint32_t *pid_out) {
    return create_process(request, pid_out, false);
}

int32_t process_create_init(void) {
    struct EXT2DriverRequest request = {.name = "shell", .name_len = 5, .parent_inode = 1};
    return create_process(request, NULL, true);
}
