#ifndef _RAM_DISK_H
#define _RAM_DISK_H

#include <stdint.h>

int ram_disk_load(const char *path);

int ram_disk_blank(uint32_t size);

int ram_disk_save(const char *path);

extern unsigned long ram_disk_reads, ram_disk_writes;

#endif
