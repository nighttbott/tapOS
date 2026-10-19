#ifndef _FS_CASES_H
#define _FS_CASES_H

#include "fs_test.h"

// read tests on test/fixtures/sample-image.bin (C's lookup, read, read_directory)
void fs_suite_team_fixture(struct FsTestContext *ctx);

// read test on test/fixtures/boundary.img: size edges, indirect blocks, nesting, inode 112/113
void fs_suite_boundary(struct FsTestContext *ctx);

// format a zeroed disk, then CRUD, restart, and fill it
void fs_suite_blank(struct FsTestContext *ctx);

// initialize on a valid disk, must not change a single byte
void fs_suite_existing(struct FsTestContext *ctx);

// two-boot persistence: writes and reads back
void fs_suite_persist_write(struct FsTestContext *ctx);
void fs_suite_persist_read(struct FsTestContext *ctx);

// raw disk deliver round trip at the end of the disk, boot 1 writes, boot 2 reads
void fs_suite_disk_write(struct FsTestContext *ctx);
void fs_suite_disk_read(struct FsTestContext *ctx); 

#endif