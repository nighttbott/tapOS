#include "header/cpu/syscall.h"
#include "header/memory/swap.h"
#include "header/process/process.h"
#include "header/stdlib/string.h"
#include "syscall_cases.h"

extern struct PageManagerState page_manager_state;

/* init's flat image is mapped at 0..256 KiB, so these addresses are valid user memory */
#define USER_BUFFER ((struct SyscallProcessInfo *)0x2000u)

static void check(struct FsTestContext *ctx, const char *id, const char *name, bool result) {
    fs_test_check(ctx, id, name, result, 1, result);
}

static int32_t call(uint32_t service, uint32_t ebx, uint32_t ecx) {
    struct InterruptFrame frame = {0};

    frame.cpu.general.eax = service;
    frame.cpu.general.ebx = ebx;
    frame.cpu.general.ecx = ecx;
    syscall_dispatch(&frame);
    return (int32_t)frame.cpu.general.eax;
}

static uint32_t create_app(void) {
    uint32_t pid = 0;

    process_create_user_process((struct EXT2DriverRequest){
        .name = "app", .name_len = 3, .parent_inode = 1,
    }, &pid);
    return pid;
}

void syscall_suite(struct FsTestContext *ctx) {
    paging_initialize();
    initialize_filesystem_ext2();
    process_initialize();

    struct PageDirectory *kernel = paging_get_current_page_directory_addr();
    struct ProcessControlBlock *init = NULL;
    bool booted = process_create_init() == PROCESS_CREATE_SUCCESS &&
                  (init = process_lookup(PROCESS_INIT_PID)) != NULL && process_set_running(init);
    check(ctx, "S01", "init is running before the syscalls are tried", booted);
    if (!booted)
        return;
    paging_use_page_directory(init->context.page_directory_virtual_addr);
    uint32_t base_frames = page_manager_state.free_page_frame_count;
    uint32_t base_swap = swap_manager.free_count;

    struct SyscallProcessInfo *list = USER_BUFFER;
    check(ctx, "S02", "ps with only init lists one running shell",
          call(SYSCALL_PROCESS_LIST, (uint32_t)list, 16) == 1 && list[0].pid == PROCESS_INIT_PID &&
          list[0].state == PROCESS_RUNNING && memcmp(list[0].name, "shell", 6) == 0);

    uint32_t first = create_app();
    uint32_t second = create_app();
    check(ctx, "S03", "ps lists init and both new ready processes",
          first != 0 && second != 0 && call(SYSCALL_PROCESS_LIST, (uint32_t)list, 16) == 3 &&
          list[1].pid == first && list[2].pid == second && list[1].state == PROCESS_READY &&
          memcmp(list[2].name, "app", 4) == 0);
    check(ctx, "S04", "ps stops at the capacity it is given",
          call(SYSCALL_PROCESS_LIST, (uint32_t)list, 2) == 2 &&
          call(SYSCALL_PROCESS_LIST, (uint32_t)list, 0) == 0);
    check(ctx, "S05", "ps refuses kernel, unmapped and kernel-crossing buffers",
          call(SYSCALL_PROCESS_LIST, KERNEL_VIRTUAL_ADDRESS_BASE, 4) == -1 &&
          call(SYSCALL_PROCESS_LIST, 0x80000000u, 4) == -1 &&
          call(SYSCALL_PROCESS_LIST, KERNEL_VIRTUAL_ADDRESS_BASE - 16u, 16) == -1);

    check(ctx, "S06", "kill refuses init", call(SYSCALL_KILL, PROCESS_INIT_PID, 0) == 2 &&
          process_lookup(PROCESS_INIT_PID) == init);
    check(ctx, "S07", "kill reports PID 0 and an unused PID as not found",
          call(SYSCALL_KILL, 0, 0) == 1 && call(SYSCALL_KILL, 9999, 0) == 1);
    check(ctx, "S08", "kill removes a ready process and leaves the other",
          call(SYSCALL_KILL, first, 0) == 0 && process_lookup(first) == NULL &&
          process_lookup(second) != NULL && call(SYSCALL_PROCESS_LIST, (uint32_t)list, 16) == 2);
    check(ctx, "S09", "killing the same PID twice reports not found",
          call(SYSCALL_KILL, first, 0) == 1);
    check(ctx, "S10", "killing every process returns frames and swap to the baseline",
          call(SYSCALL_KILL, second, 0) == 0 &&
          page_manager_state.free_page_frame_count == base_frames &&
          swap_manager.free_count == base_swap);

    paging_use_page_directory(kernel);
}
