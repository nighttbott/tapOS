#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "header/filesystem/ext2.h"

/* The host inserter edits only the EXT2 area and saves it after all writes succeed. */
static uint8_t image[DISK_SPACE];
static uint8_t data[DISK_SPACE];

void read_blocks(void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    memcpy(ptr, image + logical_block_address * BLOCK_SIZE, block_count * BLOCK_SIZE);
}

void write_blocks(const void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    memcpy(image + logical_block_address * BLOCK_SIZE, ptr, block_count * BLOCK_SIZE);
}

static int insert_path(const char *path, const char *name, uint32_t parent);

static int insert_children(const char *path, uint32_t parent) {
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
        status = insert_path(next, name, parent);
        free(next);
        if (status != 0)
            break;
    }
    if (closedir(dir) != 0)
        status = 1;
    return status;
}

static int insert_path(const char *path, const char *name, uint32_t parent) {
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
        .buf = data,
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
        request.buffer_size = (uint32_t)fread(data, 1, sizeof(data), file);
        bool input_ok = fgetc(file) == EOF && !ferror(file);
        if (fclose(file) != 0 || !input_ok) {
            fprintf(stderr, "inserter: cannot read %s or file exceeds %u bytes\n", path, DISK_SPACE);
            return 1;
        }
    }

    int8_t result = write(&request);
    if (result == 1 && !request.is_directory && delete(request) == 0)
        result = write(&request);
    if (result != 0) {
        fprintf(stderr, "inserter: write %s failed with code %d\n", path, result);
        return 1;
    }
    if (!request.is_directory)
        return 0;

    request.buffer_size = sizeof(data);
    if (read_directory(&request) != 0) {
        fprintf(stderr, "inserter: cannot read new directory %s\n", path);
        return 1;
    }
    struct EXT2DirectoryEntry dot;
    memcpy(&dot, data, sizeof(dot));
    return insert_children(path, dot.inode);
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s <file-or-directory> <parent_inode> <storage.bin>\n", argv[0]);
        return 2;
    }
    char *end;
    errno = 0;
    unsigned long parent = strtoul(argv[2], &end, 10);
    if (argv[2][0] < '0' || argv[2][0] > '9' || *end != '\0' ||
        errno != 0 || parent == 0 || parent > UINT32_MAX) {
        fputs("inserter: invalid parent inode\n", stderr);
        return 2;
    }

    char *path = argv[1];
    size_t path_len = strlen(path);
    while (path_len > 1 && path[path_len - 1] == '/')
        path[--path_len] = '\0';
    const char *separator = strrchr(path, '/');
    const char *name = separator ? separator + 1 : path;

    FILE *file = fopen(argv[3], "rb+");
    if (file == NULL || fread(image, 1, sizeof(image), file) != sizeof(image)) {
        fprintf(stderr, "inserter: cannot read %u bytes from %s (make disk)\n",
                DISK_SPACE, argv[3]);
        if (file != NULL)
            fclose(file);
        return 1;
    }

    if (is_empty_storage()) {
        for (size_t i = 0; i < sizeof(image); ++i) {
            if (image[i] != 0) {
                fprintf(stderr, "inserter: unrecognized filesystem in %s\n", argv[3]);
                fclose(file);
                return 1;
            }
        }
    }
    initialize_filesystem_ext2();
    if (insert_path(path, name, (uint32_t)parent) != 0) {
        fclose(file);
        return 1;
    }

    bool saved = fseek(file, 0, SEEK_SET) == 0 &&
                 fwrite(image, 1, sizeof(image), file) == sizeof(image);
    if (fclose(file) != 0 || !saved) {
        fprintf(stderr, "inserter: cannot write %s\n", argv[3]);
        return 1;
    }
    printf("inserted %s into inode %lu of %s\n", name, parent, argv[3]);
    return 0;
}
