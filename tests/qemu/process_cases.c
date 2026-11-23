#include "header/cpu/gdt.h"
#include "header/memory/swap.h"
#include "header/process/process.h"
#include "process_cases.h"

extern struct PageManagerState page_manager_state;

static void check(struct FsTestContext *ctx, const char *id, const char *name, bool result) {
    fs_test_check(ctx, id, name, result, 1, result);
}

static int32_t create_app(uint32_t *pid) {
    return process_create_user_process((struct EXT2DriverRequest){
        .name = "app", .name_len = 3, .parent_inode = 1,
    }, pid);
}

static void check_context(struct FsTestContext *ctx, struct ProcessControlBlock *pcb) {
    struct UserInterruptFrame frame = {
        .frame = {
            .cpu.stack = {.ebp = 0x12345678u, .esp = 0xC0123456u},
            .cpu.general.eax = 0xABCDEF01u,
            .cpu.segment.ds = 0xA5A50023u,
            .int_stack = {.eip = 0x100u, .cs = GDT_KERNEL_CODE_SEGMENT_SELECTOR, .eflags = 0x202u},
        },
        .esp = KERNEL_VIRTUAL_ADDRESS_BASE - 32u,
        .ss = 0xAAAA0023u,
    };

    check(ctx, "P11", "Ring 0 frame cannot replace the user context",
          !process_save_interrupt_context(&frame.frame) && pcb->context.eip == 0);
    frame.frame.int_stack.cs = GDT_USER_CODE_SEGMENT_SELECTOR;
    check(ctx, "P12", "Ring 3 trap stores user ESP, selectors, flags and registers",
          process_save_interrupt_context(&frame.frame) && pcb->context.eip == 0x100u &&
          pcb->context.eflags == 0x202u && pcb->context.cs == GDT_USER_CODE_SEGMENT_SELECTOR &&
          pcb->context.ss == GDT_USER_DATA_SEGMENT_SELECTOR &&
          pcb->context.cpu.stack.ebp == 0x12345678u && pcb->context.cpu.stack.esp == frame.esp &&
          pcb->context.cpu.general.eax == 0xABCDEF01u && pcb->context.cpu.segment.ds == 0x23u);
}

void process_suite(struct FsTestContext *ctx) {
    paging_initialize();
    initialize_filesystem_ext2();
    process_initialize();

    struct PageDirectory *kernel = paging_get_current_page_directory_addr();
    uint32_t start_frames = page_manager_state.free_page_frame_count;
    uint32_t start_swap = swap_manager.free_count;
    int32_t status = process_create_init();
    struct ProcessControlBlock *init = process_lookup(PROCESS_INIT_PID);
    check(ctx, "P01", "boot shell becomes ready as PID 1", status == PROCESS_CREATE_SUCCESS &&
          init != NULL && init->metadata.state == PROCESS_READY && process_set_running(init));
    if (init == NULL)
        return;

    struct PageDirectory *init_dir = init->context.page_directory_virtual_addr;
    paging_use_page_directory(init_dir);
    uint32_t free_frames = page_manager_state.free_page_frame_count;
    check(ctx, "P02", "protected init and active page directory cannot be destroyed",
          !process_destroy(PROCESS_INIT_PID) && !paging_free_page_directory(init_dir));

    struct EXT2DriverRequest missing = {.name = "missing", .name_len = 7, .parent_inode = 1};
    struct EXT2DriverRequest invalid = {.name = "shell", .name_len = 5, .parent_inode = 1};
    uint32_t result_pid = 0;
    check(ctx, "P03", "missing executable returns FS failure",
          process_create_user_process(missing, &result_pid) == PROCESS_CREATE_FAIL_FS_READ_FAILURE);
    check(ctx, "P04", "raw file is rejected as executable by exec",
          process_create_user_process(invalid, &result_pid) == PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT);
    check(ctx, "P05", "failed loads restore caller address space and publish no PID",
          result_pid == 0 && paging_get_current_page_directory_addr() == init_dir &&
          page_manager_state.free_page_frame_count == free_frames);

    uint32_t held[PAGE_FRAME_MAX_COUNT];
    for (uint32_t i = 0; i < free_frames; i++)
        held[i] = paging_acquire_frame();
    check(ctx, "P06", "low memory refuses creation without switching away from caller",
          create_app(&result_pid) == PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY &&
          result_pid == 0 && paging_get_current_page_directory_addr() == init_dir &&
          page_manager_state.free_page_frame_count == 0);
    for (uint32_t i = 0; i < free_frames; i++)
        paging_release_frame(held[i]);

    uint32_t first_pid = 0;
    uint32_t second_pid = 0;
    bool created = create_app(&first_pid) == PROCESS_CREATE_SUCCESS &&
                   create_app(&second_pid) == PROCESS_CREATE_SUCCESS;
    struct ProcessControlBlock *first = process_lookup(first_pid);
    struct ProcessControlBlock *second = process_lookup(second_pid);
    check(ctx, "P07", "two instances get different ready PCBs and isolated page directories",
          created && first != NULL && second != NULL && first != second &&
          first->metadata.state == PROCESS_READY && second->metadata.state == PROCESS_READY &&
          first->context.page_directory_virtual_addr != second->context.page_directory_virtual_addr);
    if (first == NULL || second == NULL || first == second)
        return;

    bool running = process_set_running(first);
    struct PageDirectory *first_dir = first->context.page_directory_virtual_addr;
    paging_use_page_directory(first_dir);
    volatile uint32_t *data = (void *)0x1000u;
    volatile uint32_t *bss = (void *)0x2000u;
    check(ctx, "P08", "ELF entry, access flags, data and zeroed BSS match the binary",
          running && first->context.eip == 0 && first->context.eflags == 0x202u &&
          first->context.cpu.stack.esp == KERNEL_VIRTUAL_ADDRESS_BASE &&
          *data == 0x12345678u && *bss == 0 &&
          !paging_get_page_table_entry(first_dir, NULL)->flag.write_bit &&
          paging_get_page_table_entry(first_dir, (void *)data)->flag.write_bit);
    *data = 0xABCDEF01u;
    bool swapped = swap_out(first_dir, (void *)data);

    struct PageDirectory *second_dir = second->context.page_directory_virtual_addr;
    paging_use_page_directory(second_dir);
    check(ctx, "P09", "writing and swapping the first process page leaves the second intact",
          swapped && *data == 0x12345678u && swap_manager.free_count == start_swap - 1);
    paging_use_page_directory(first_dir);

    check(ctx, "P10", "illegal transition and reaping a running process are refused",
          !process_mark_ready(first) && !process_reap(first_pid));
    check_context(ctx, first);
    check(ctx, "P13", "current exit records status and stays unreapable while CR3 is live",
          process_terminate(first_pid, 42) && first->metadata.exit_code == 42 &&
          first->metadata.state == PROCESS_TERMINATED && !process_reap(first_pid) &&
          process_get_current_running_pcb_pointer() == NULL);

    paging_use_page_directory(init_dir);
    check(ctx, "P14", "inactive process reap and destroy restore physical frames and swap",
          process_set_running(init) && process_reap(first_pid) && process_destroy(second_pid) &&
          process_lookup(first_pid) == NULL && process_lookup(second_pid) == NULL &&
          page_manager_state.free_page_frame_count == free_frames && swap_manager.free_count == start_swap);

    uint32_t ids[PROCESS_COUNT_MAX - 1] = {0};
    bool filled = true;
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX - 1; i++)
        filled = create_app(&ids[i]) == PROCESS_CREATE_SUCCESS && filled;
    result_pid = 0;
    check(ctx, "P15", "table-full failure preserves output and rejects invalid PID",
          filled && create_app(&result_pid) == PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED &&
          result_pid == 0 && !process_destroy(0));

    bool reused = process_destroy(ids[0]);
    for (uint32_t i = 0; i < 64 && reused; i++) {
        uint32_t new_pid = 0;
        reused = create_app(&new_pid) == PROCESS_CREATE_SUCCESS && new_pid > ids[0] &&
                 process_destroy(new_pid);
    }
    check(ctx, "P16", "64 create/destroy cycles reuse the only free slot", reused);
    bool cleared = true;
    for (uint32_t i = 1; i < PROCESS_COUNT_MAX - 1; i++)
        cleared = process_destroy(ids[i]) && cleared;
    check(ctx, "P17", "all non-init resources return to baseline", cleared &&
          page_manager_state.free_page_frame_count == free_frames &&
          swap_manager.free_count == start_swap && free_frames < start_frames);
    paging_use_page_directory(kernel);
}
