#include "header/filesystem/ext2.h"
#include "fs_test.h"

/*
 * placeholders for filesystem functions.
 * "weak" means: if ext2.c has the real function, the linker uses the real one
 * and ignores this. Each placeholder bumps fs_stub_hits, so tests can report
 * "not implemented yet" instead of a fake pass.
 */

__attribute__((weak)) void initialize_filesystem_ext2(void) {
    fs_stub_hits++;
}

__attribute__((weak)) int8_t write(struct EXT2DriverRequest *request) {
    (void) request;
    fs_stub_hits++;
    return FS_TEST_NOT_IMPLEMENTED;
}

__attribute__((weak)) int8_t delete(struct EXT2DriverRequest request) {
    (void) request;
    fs_stub_hits++;
    return FS_TEST_NOT_IMPLEMENTED;
}