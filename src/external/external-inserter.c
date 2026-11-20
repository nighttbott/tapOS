#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "header/filesystem/ext2.h"

/* Based on the Chapter 3 inserter kit. Only the EXT2 region is held in memory. */
static uint8_t image_storage[DISK_SPACE];
static uint8_t file_buffer[DISK_SPACE];

void read_blocks(void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    for (uint32_t i = 0; i < block_count; i++) {
        memcpy((uint8_t *)ptr + BLOCK_SIZE * i,
               image_storage + BLOCK_SIZE * (logical_block_address + i),
               BLOCK_SIZE);
    }
}

void write_blocks(const void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    for (uint32_t i = 0; i < block_count; i++) {
        memcpy(image_storage + BLOCK_SIZE * (logical_block_address + i),
               (const uint8_t *)ptr + BLOCK_SIZE * i,
               BLOCK_SIZE);
    }
}

static int insert_path(const char *path, const char *name, uint32_t parent, bool replace);

static int insert_children(const char *path, uint32_t parent, bool replace) {
    DIR *dir = opendir(path);
    if (dir == NULL) {
        fprintf(stderr, "inserter: cannot open directory %s\n", path);
        return 1;
    }

    int status = 0;
    size_t path_len = strlen(path);
    for (;;) {
        errno = 0;
        struct dirent *child = readdir(dir);
        if (child == NULL) {
            if (errno != 0) {
                fprintf(stderr, "inserter: cannot list %s\n", path);
                status = 1;
            }
            break;
        }
        const char *name = child->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;

        size_t size = path_len + strlen(name) + 2;
        char *next = malloc(size);
        if (next == NULL) {
            fputs("inserter: out of memory\n", stderr);
            status = 1;
            break;
        }
        snprintf(next, size, "%s/%s", path, name);
        status = insert_path(next, name, parent, replace);
        free(next);
        if (status != 0)
            break;
    }
    if (closedir(dir) != 0)
        status = 1;
    return status;
}

static int insert_path(const char *path, const char *name, uint32_t parent, bool replace) {
    size_t name_len = strlen(name);
    if (name_len == 0 || name_len > UINT8_MAX ||
        strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        fprintf(stderr, "inserter: bad entry name '%s'\n", name);
        return 1;
    }

    struct stat info;
    if (lstat(path, &info) != 0 ||
        (!S_ISREG(info.st_mode) && !S_ISDIR(info.st_mode))) {
        fprintf(stderr, "inserter: expected a regular file or directory: %s\n", path);
        return 1;
    }

    struct EXT2DriverRequest request = {
        .buf = file_buffer,
        .name = (char *)name,
        .name_len = (uint8_t)name_len,
        .parent_inode = parent,
        .is_directory = S_ISDIR(info.st_mode),
    };
    if (!request.is_directory) {
        FILE *file = fopen(path, "rb");
        if (file == NULL) {
            fprintf(stderr, "inserter: cannot open %s\n", path);
            return 1;
        }
        request.buffer_size = (uint32_t)fread(file_buffer, 1, sizeof(file_buffer), file);
        bool input_ok = fgetc(file) == EOF && !ferror(file);
        if (fclose(file) != 0 || !input_ok) {
            fprintf(stderr, "inserter: cannot read %s or file exceeds %u bytes\n", path, DISK_SPACE);
            return 1;
        }
    }

    int8_t result = write(&request);
    if (result == 1 && replace && !request.is_directory && delete(request) == 0)
        result = write(&request);
    if (result != 0) {
        fprintf(stderr, "inserter: write %s failed with code %d\n", path, result);
        return 1;
    }
    if (!request.is_directory)
        return 0;

    request.buffer_size = sizeof(file_buffer);
    if (read_directory(&request) != 0) {
        fprintf(stderr, "inserter: cannot read new directory %s\n", path);
        return 1;
    }
    struct EXT2DirectoryEntry dot;
    memcpy(&dot, file_buffer, sizeof(dot));
    return insert_children(path, dot.inode, replace);
}

int main(int argc, char **argv) {
    int argi = 1;
    bool replace = argc > argi && strcmp(argv[argi], "-f") == 0;
    if (replace)
        argi++;
    if (argc - argi != 3) {
        fprintf(stderr, "usage: %s [-f] <file-or-directory> <parent_inode> <storage.bin>\n", argv[0]);
        return 2;
    }
    char *path = argv[argi];
    const char *parent_str = argv[argi + 1];
    const char *storage_path = argv[argi + 2];

    char *end;
    errno = 0;
    unsigned long parent = strtoul(parent_str, &end, 10);
    if (parent_str[0] < '0' || parent_str[0] > '9' || *end != '\0' ||
        errno != 0 || parent == 0 || parent > UINT32_MAX) {
        fputs("inserter: invalid parent inode\n", stderr);
        return 2;
    }

    size_t path_len = strlen(path);
    while (path_len > 1 && path[path_len - 1] == '/')
        path[--path_len] = '\0';
    const char *separator = strrchr(path, '/');
    const char *name = separator ? separator + 1 : path;

    FILE *file = fopen(storage_path, "rb+");
    if (file == NULL || fread(image_storage, 1, sizeof(image_storage), file) != sizeof(image_storage)) {
        fprintf(stderr, "inserter: cannot read %u bytes from %s (make disk)\n",
                DISK_SPACE, storage_path);
        if (file != NULL)
            fclose(file);
        return 1;
    }

    if (is_empty_storage()) {
        for (size_t i = 0; i < sizeof(image_storage); ++i) {
            if (image_storage[i] != 0) {
                fprintf(stderr, "inserter: unrecognized filesystem in %s\n", storage_path);
                fclose(file);
                return 1;
            }
        }
    }
    initialize_filesystem_ext2();
    if (insert_path(path, name, (uint32_t)parent, replace) != 0) {
        fclose(file);
        return 1;
    }

    bool saved = fseek(file, 0, SEEK_SET) == 0 &&
                 fwrite(image_storage, 1, sizeof(image_storage), file) == sizeof(image_storage);
    if (fclose(file) != 0 || !saved) {
        fprintf(stderr, "inserter: cannot write %s\n", storage_path);
        return 1;
    }
    printf("inserted %s into inode %lu of %s\n", name, parent, storage_path);
    return 0;
}
