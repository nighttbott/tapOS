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
    SYSCALL_READ_DIRECTORY_BLOCK = 13,
    SYSCALL_MOVE = 14,
    SYSCALL_FIND = 15,
    SYSCALL_GET_TICKS = 24,   /* PIT ticks since boot, 100 per second */
    SYSCALL_READ_RANGE = 25,  /* like READ_FILE from byte offset ecx; returns bytes read */
    SYSCALL_BLIT_BITS = 26,   /* ebx = 1 bit per pixel image, ecx = width, edx = height */
    SYSCALL_REDRAW = 27,      /* paint the text screen again after BLIT_BITS */
};

/* SYSCALL_FIND: paths of every entry called `name` in the whole file system, one per line. */
struct SyscallFindRequest {
    const char *name;
    uint8_t     name_len;
    char       *out;
    uint32_t    out_size;
    uint32_t    written;    /* filled in by the kernel */
} __attribute__((packed));

#define SYSCALL_PROCESS_NAME_MAX 32

struct SyscallProcessInfo {
    uint32_t pid;
    uint32_t state;
    char     name[SYSCALL_PROCESS_NAME_MAX];
} __attribute__((packed));

void syscall_dispatch(struct InterruptFrame *frame);

#endif
