#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "header/driver/disk.h"
#include "ram_disk.h"

static uint8_t *disk;
static size_t disk_size;
unsigned long ram_disk_reads, ram_disk_writes;

int ram_disk_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return -1;
    fseek(f, 0, SEEK_END);
    disk_size = (size_t) ftell(f);
    rewind(f);
    free(disk);
    disk = malloc(disk_size);
    size_t got = fread(disk, 1, disk_size, f);
    fclose(f);
    return got == disk_size ? 0 : -1;
}

int ram_disk_blank(uint32_t size) {
    free(disk);
    disk_size = size;
    disk = calloc(1, disk_size);
    return disk == NULL ? -1 : 0;
}

int ram_disk_save(const char *path) {
    FILE *f = fopen(path, "wb");
    if (f == NULL)
        return -1;
    size_t put = fwrite(disk, 1, disk_size, f);
    fclose(f);
    return put == disk_size ? 0 : -1;
}

static void check_range(const char *op, uint32_t lba, uint8_t count) {
    if ((size_t) (lba + count) * BLOCK_SIZE > disk_size) {
        fprintf(stderr, "ram_disk: %s past end of disk, lba=%u count=%u\n", op, lba, count);
        abort();
    }
}

void read_blocks(void *ptr, uint32_t logical_block_addr, uint8_t block_cnt) {
    check_range("read", logical_block_addr, block_cnt);
    ram_disk_reads++;
    memcpy(ptr, disk + (size_t) logical_block_addr * BLOCK_SIZE, (size_t) block_cnt * BLOCK_SIZE);
}

void write_blocks(const void *ptr, uint32_t logical_block_addr, uint8_t block_cnt) {
    check_range("write", logical_block_addr, block_cnt);
    ram_disk_writes++;
    memcpy(disk + (size_t) logical_block_addr * BLOCK_SIZE, ptr, (size_t) block_cnt * BLOCK_SIZE);
}
