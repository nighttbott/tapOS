#ifndef _PROCESS_H
#define _PROCESS_H

#include "header/cpu/interrupt.h"
#include "header/filesystem/ext2.h"
#include "header/memory/paging.h"

#define PROCESS_NAME_LENGTH_MAX 32
#define PROCESS_PAGE_FRAME_COUNT_MAX 8
#define PROCESS_COUNT_MAX 16
#define PROCESS_INIT_PID 1u

#define CPU_EFLAGS_BASE_FLAG 0x2u
#define CPU_EFLAGS_FLAG_INTERRUPT_ENABLE 0x200u

enum ProcessCreateResult {
    PROCESS_CREATE_SUCCESS = 0,
    PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED = 1,
    PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT = 2,
    PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY = 3,
    PROCESS_CREATE_FAIL_FS_READ_FAILURE = 4,
};

typedef enum PROCESS_STATE {
    PROCESS_NEW,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_TERMINATED,
    PROCESS_UNUSED,
} PROCESS_STATE;

struct Context {
    struct CPURegister cpu;
    uint32_t eip, eflags, cs, ss;
    struct PageDirectory *page_directory_virtual_addr;
};

struct ProcessControlBlock {
    struct {
        uint32_t pid;
        PROCESS_STATE state;
        char name[PROCESS_NAME_LENGTH_MAX + 1];
        int32_t exit_code;
    } metadata;
    struct Context context;
};

extern struct ProcessControlBlock _process_list[PROCESS_COUNT_MAX];

/* Call process management with maskable interrupts disabled. Initialize once at boot.
 * PCB pointers remain valid as identities only until the slot is released. */
void process_initialize(void);
struct ProcessControlBlock *process_lookup(uint32_t pid);
struct ProcessControlBlock *process_get_current_running_pcb_pointer(void);
struct ProcessControlBlock *process_reserve(const char *name, uint8_t name_len, bool init);
bool process_mark_ready(struct ProcessControlBlock *pcb);
bool process_set_running(struct ProcessControlBlock *next);
bool process_terminate(uint32_t pid, int32_t exit_code);
bool process_release(struct ProcessControlBlock *pcb);
bool process_save_interrupt_context(const struct InterruptFrame *frame);

#endif
