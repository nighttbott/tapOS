#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#include "header/process/process.h"

/* Assembly restores this user context and returns with iret. */
__attribute__((noreturn)) void process_context_switch(const struct Context *context);

void scheduler_init(void);
void scheduler_save_context_to_current_running_pcb(struct Context context);
__attribute__((noreturn)) void scheduler_switch_to_next_process(void);

/* Called by the IRQ0 handler with interrupts disabled. */
void scheduler_handle_timer(struct InterruptFrame *frame);

#endif
