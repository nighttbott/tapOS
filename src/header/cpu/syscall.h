#ifndef _SYSCALL_H
#define _SYSCALL_H

#include <stdint.h>
#include "header/cpu/interrupt.h"

#define SYSCALL_VECTOR 0x30u

enum SyscallNumber {
    SYSCALL_WRITE_TEXT = 1,
    SYSCALL_READ_CHAR = 2,
    SYSCALL_READ_FILE = 3,
    SYSCALL_READ_DIRECTORY = 4,
    SYSCALL_WRITE_FILE = 5,
    SYSCALL_DELETE_FILE = 6,
};

/* Dispatch a Ring 3 request. EAX selects a SyscallNumber; EBX is its argument. */
void syscall_dispatch(struct InterruptFrame *frame);

#endif
