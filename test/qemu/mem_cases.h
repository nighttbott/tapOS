#ifndef _MEM_CASES_H
#define _MEM_CASES_H

#include "fs_test.h"

// SYSTEM_MEMORY_MB and QEMU -m must be equal
void mem_suite_frames(struct FsTestContext *ctx);
void mem_suite_fault(struct FsTestContext *ctx);
// Needs a 5 MiB disk
void mem_suite_swap(struct FsTestContext *ctx);

#endif
