#ifndef _MEM_CASES_H
#define _MEM_CASES_H

#include "fs_test.h"

void mem_suite_frames(struct FsTestContext *ctx);
void mem_suite_fault(struct FsTestContext *ctx);
void mem_suite_swap(struct FsTestContext *ctx);
void mem_suite_load(struct FsTestContext *ctx);

#endif
