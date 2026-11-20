#include "header/process/process.h"
#include "header/stdlib/string.h"

struct ProcessControlBlock _process_list[PROCESS_COUNT_MAX];
static struct ProcessControlBlock *current_process;
static uint32_t next_pid;

void process_initialize(void) {
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++)
        _process_list[i].metadata.state = PROCESS_UNUSED;
    current_process = NULL;
    next_pid = PROCESS_INIT_PID + 1;
}

struct ProcessControlBlock *process_lookup(uint32_t pid) {
    if (pid == 0)
        return NULL;
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        struct ProcessControlBlock *pcb = &_process_list[i];
        if (pcb->metadata.state != PROCESS_UNUSED && pcb->metadata.pid == pid)
            return pcb;
    }
    return NULL;
}

struct ProcessControlBlock *process_get_current_running_pcb_pointer(void) {
    return current_process;
}

struct ProcessControlBlock *process_reserve(const char *name, uint8_t name_len, bool init) {
    if (name == NULL || name_len == 0 || name_len > PROCESS_NAME_LENGTH_MAX ||
        (init && process_lookup(PROCESS_INIT_PID) != NULL))
        return NULL;

    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        struct ProcessControlBlock *pcb = &_process_list[i];
        if (pcb->metadata.state != PROCESS_UNUSED)
            continue;

        uint32_t pid = PROCESS_INIT_PID;
        if (!init) {
            do {
                pid = next_pid++;
                if (next_pid == 0)
                    next_pid = PROCESS_INIT_PID + 1;
            } while (process_lookup(pid) != NULL);
        }
        *pcb = (struct ProcessControlBlock){0};
        pcb->metadata.pid = pid;
        pcb->metadata.state = PROCESS_NEW;
        memcpy(pcb->metadata.name, name, name_len);
        return pcb;
    }
    return NULL;
}

bool process_mark_ready(struct ProcessControlBlock *pcb) {
    if (pcb == NULL || pcb->metadata.state != PROCESS_NEW ||
        pcb->context.page_directory_virtual_addr == NULL)
        return false;
    pcb->metadata.state = PROCESS_READY;
    return true;
}

bool process_set_running(struct ProcessControlBlock *next) {
    if (current_process == next)
        return true;
    if (next != NULL && next->metadata.state != PROCESS_READY)
        return false;
    if (current_process != NULL)
        current_process->metadata.state = PROCESS_READY;
    current_process = next;
    if (next != NULL)
        next->metadata.state = PROCESS_RUNNING;
    return true;
}

bool process_terminate(uint32_t pid, int32_t exit_code) {
    struct ProcessControlBlock *pcb = process_lookup(pid);
    if (pcb == NULL || pid == PROCESS_INIT_PID ||
        (pcb->metadata.state != PROCESS_READY && pcb->metadata.state != PROCESS_RUNNING))
        return false;
    if (pcb == current_process)
        current_process = NULL;
    pcb->metadata.exit_code = exit_code;
    pcb->metadata.state = PROCESS_TERMINATED;
    return true;
}

bool process_release(struct ProcessControlBlock *pcb) {
    if (pcb == NULL || pcb->context.page_directory_virtual_addr != NULL ||
        (pcb->metadata.state != PROCESS_NEW && pcb->metadata.state != PROCESS_TERMINATED))
        return false;
    *pcb = (struct ProcessControlBlock){.metadata.state = PROCESS_UNUSED};
    return true;
}

bool process_save_interrupt_context(const struct InterruptFrame *frame) {
    if (frame == NULL || (frame->int_stack.cs & 3u) != 3u || current_process == NULL)
        return false;

    const struct UserInterruptFrame *user = (const struct UserInterruptFrame *)frame;
    struct Context *ctx = &current_process->context;
    ctx->cpu = frame->cpu;
    ctx->cpu.stack.esp = user->esp;
    ctx->cpu.segment.ds &= 0xFFFFu;
    ctx->cpu.segment.es &= 0xFFFFu;
    ctx->cpu.segment.fs &= 0xFFFFu;
    ctx->cpu.segment.gs &= 0xFFFFu;
    ctx->eip = frame->int_stack.eip;
    ctx->eflags = frame->int_stack.eflags;
    ctx->cs = frame->int_stack.cs & 0xFFFFu;
    ctx->ss = user->ss & 0xFFFFu;
    return true;
}
