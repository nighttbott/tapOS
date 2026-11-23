#ifndef _FS_CASES_H
#define _FS_CASES_H

#include "fs_test.h"

void fs_suite_team_fixture(struct FsTestContext *ctx);

void fs_suite_boundary(struct FsTestContext *ctx);

void fs_suite_delete(struct FsTestContext *ctx);

void fs_suite_blank(struct FsTestContext *ctx);

void fs_suite_existing(struct FsTestContext *ctx);

void fs_suite_persist_write(struct FsTestContext *ctx);
void fs_suite_persist_read(struct FsTestContext *ctx);

void fs_suite_disk_write(struct FsTestContext *ctx);
void fs_suite_disk_read(struct FsTestContext *ctx); 

#endif
