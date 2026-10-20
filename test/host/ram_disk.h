#ifndef _RAM_DISK_H
#define _RAM_DISK_H

#include <stdint.h>

// load a disk image file into memory, the file itself never changed. 0 = ok
int ram_disk_load(const char *path);

// start with all-zero disk
int ram_disk_blank(uint32_t size);

// write the in-memory disk to a file. 0 = ok
int ram_disk_save(const char *path);

// how many read_blocks / write_blocks call happened
extern unsigned long ram_disk_reads, ram_disk_writes;

#endif