#include <stdio.h>
#include <string.h>
#include "header/filesystem/ext2.h"

/*
 * Copies a host file into the EXT2 area of a disk image, replacing any file
 * with the same name.  inserter <file> <parent_inode> <storage.bin>
 * The entry name is the file's base name. Only the first DISK_SPACE bytes
 * (the filesystem) are touched, so the swap area behind them is kept.
 */

static uint8_t image[DISK_SPACE];
static uint8_t data[DISK_SPACE];

void read_blocks(void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    memcpy(ptr, image + logical_block_address * BLOCK_SIZE, block_count * BLOCK_SIZE);
}

void write_blocks(const void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    memcpy(image + logical_block_address * BLOCK_SIZE, ptr, block_count * BLOCK_SIZE);
}

int main(int argc, char **argv) {
    const char *name;
    uint32_t parent;
    size_t size;
    FILE *file;
    int8_t result;

    if (argc != 4 || sscanf(argv[2], "%u", &parent) != 1) {
        fprintf(stderr, "usage: %s <file> <parent_inode> <storage.bin>\n", argv[0]);
        return 2;
    }
    name = strrchr(argv[1], '/') != NULL ? strrchr(argv[1], '/') + 1 : argv[1];
    if (strlen(name) == 0 || strlen(name) > 255) {
        fprintf(stderr, "inserter: bad file name '%s'\n", name);
        return 2;
    }

    file = fopen(argv[1], "rb");
    if (file == NULL) {
        fprintf(stderr, "inserter: cannot open %s\n", argv[1]);
        return 1;
    }
    size = fread(data, 1, sizeof(data), file);
    fclose(file);

    file = fopen(argv[3], "rb+");
    if (file == NULL || fread(image, 1, sizeof(image), file) != sizeof(image)) {
        fprintf(stderr, "inserter: %s must be at least %u bytes (make disk)\n", argv[3], DISK_SPACE);
        return 1;
    }

    initialize_filesystem_ext2();
    struct EXT2DriverRequest request = {
        .buf = data, .name = (char *) name, .name_len = (uint8_t) strlen(name),
        .parent_inode = parent, .buffer_size = (uint32_t) size, .is_directory = false,
    };
    result = write(&request);
    if (result == 1 && delete(request) == 0)
        result = write(&request);
    if (result != 0) {
        fprintf(stderr, "inserter: write %s failed with code %d\n", name, result);
        fclose(file);
        return 1;
    }

    fseek(file, 0, SEEK_SET);
    if (fwrite(image, 1, sizeof(image), file) != sizeof(image)) {
        fprintf(stderr, "inserter: cannot write %s\n", argv[3]);
        fclose(file);
        return 1;
    }
    fclose(file);
    printf("inserted %s (%zu bytes) into inode %u of %s\n", name, size, parent, argv[3]);
    return 0;
}
