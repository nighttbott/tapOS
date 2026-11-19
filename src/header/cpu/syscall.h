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
    SYSCALL_EXIT = 7,
    SYSCALL_EXEC = 8,
    SYSCALL_KILL = 9,
    SYSCALL_PROCESS_LIST = 10,
    SYSCALL_GET_TIME = 11,
    SYSCALL_WRITE_STATUS = 12,
};

/* Must equal PROCESS_NAME_LENGTH_MAX, including the '\0'. */
#define SYSCALL_PROCESS_NAME_MAX 32

/* One row of SYSCALL_PROCESS_LIST, shared with user programs. */
struct SyscallProcessInfo {
    uint32_t pid;
    uint32_t state;                         // enum ProcessState: 1 ready, 2 running
    char     name[SYSCALL_PROCESS_NAME_MAX];
} __attribute__((packed));

/* Dispatch a Ring 3 request. EAX selects a SyscallNumber; EBX, ECX and EDX are its arguments. */
void syscall_dispatch(struct InterruptFrame *frame);

#endif
