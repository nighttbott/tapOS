#include "header/cpu/scheduler.h"
#include "header/cpu/interrupt.h"

#define PIT_INPUT_HZ 1193182u
#define SCHEDULER_TICK_HZ 100u

static uint32_t next_process_index;

void scheduler_init(void) {
    next_process_index = 0;
    pit_initialize(PIT_INPUT_HZ / SCHEDULER_TICK_HZ);
}

void scheduler_save_context_to_current_running_pcb(struct Context context) {
    struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();
    if (current != NULL)
        current->context = context;
}

static struct ProcessControlBlock *scheduler_find_next_ready(void) {
    for (uint32_t offset = 0; offset < PROCESS_COUNT_MAX; offset++) {
        uint32_t index = (next_process_index + offset) % PROCESS_COUNT_MAX;
        struct ProcessControlBlock *candidate = &_process_list[index];
        if (candidate->metadata.state != PROCESS_READY ||
            candidate->context.page_directory_virtual_addr == NULL)
            continue;
        next_process_index = (index + 1u) % PROCESS_COUNT_MAX;
        return candidate;
    }

    struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();
    if (current != NULL && current->metadata.state == PROCESS_RUNNING)
        return current;
    return NULL;
}

__attribute__((noreturn))
void scheduler_switch_to_next_process(void) {
    struct ProcessControlBlock *next = scheduler_find_next_ready();
    if (next == NULL) {
        __asm__ volatile("cli");
        for (;;) __asm__ volatile("hlt");
    }
    if (!process_set_running(next)) {
        __asm__ volatile("cli");
        for (;;) __asm__ volatile("hlt");
    }
    process_context_switch(&next->context);
}

void scheduler_handle_timer(struct InterruptFrame *frame) {
    if (frame == NULL || (frame->int_stack.cs & 3u) != 3u)
        return;

    struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();
    if (current == NULL || current->metadata.state != PROCESS_RUNNING)
        return;

    if (process_save_interrupt_context(frame))
        scheduler_switch_to_next_process();
}
