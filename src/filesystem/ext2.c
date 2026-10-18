#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/filesystem/ext2.h"

const uint8_t fs_signature[BLOCK_SIZE] = {
    'C', 'o', 'u', 'r', 's', 'e', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',  ' ',
    'D', 'e', 's', 'i', 'g', 'n', 'e', 'd', ' ', 'b', 'y', ' ', ' ', ' ', ' ',  ' ',
    'L', 'a', 'b', ' ', 'S', 'i', 's', 't', 'e', 'r', ' ', 'I', 'T', 'B', ' ',  ' ',
    'M', 'a', 'd', 'e', ' ', 'w', 'i', 't', 'h', ' ', '<', '3', ' ', ' ', ' ',  ' ',
    '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '2', '0', '2', '5', '\n',
    [BLOCK_SIZE-2] = 'O',
    [BLOCK_SIZE-1] = 'k',
};

#define EXT2_DIRECT_BLOCK_COUNT 12u
#define EXT2_POINTERS_PER_BLOCK (BLOCK_SIZE / sizeof(uint32_t))
#define EXT2_SINGLE_INDIRECT_START EXT2_DIRECT_BLOCK_COUNT
#define EXT2_DOUBLE_INDIRECT_START \
    (EXT2_SINGLE_INDIRECT_START + EXT2_POINTERS_PER_BLOCK)
#define EXT2_MAX_DATA_BLOCKS \
    (EXT2_DOUBLE_INDIRECT_START + EXT2_POINTERS_PER_BLOCK * EXT2_POINTERS_PER_BLOCK)

struct EXT2ReadContext {
    struct EXT2Superblock superblock;
    struct EXT2BlockGroupDescriptorTable bgd_table;
};

static bool ext2_read_context_init(struct EXT2ReadContext *context) {
    struct BlockBuffer block;

    if (context == NULL ||
        GROUPS_COUNT * sizeof(struct EXT2BlockGroupDescriptor) > BLOCK_SIZE) {
        return false;
    }

    read_blocks(&block, 1, 1);
    memcpy(&context->superblock, block.buf, sizeof(context->superblock));

    if (context->superblock.s_magic != EXT2_SUPER_MAGIC ||
        context->superblock.s_blocks_count <= 2 ||
        context->superblock.s_blocks_count > DISK_SPACE / BLOCK_SIZE ||
        context->superblock.s_inodes_count == 0 ||
        context->superblock.s_inodes_count > INODES_PER_GROUP * GROUPS_COUNT ||
        context->superblock.s_inodes_per_group != INODES_PER_GROUP) {
        return false;
    }

    read_blocks(&block, 2, 1);
    memcpy(&context->bgd_table, block.buf, sizeof(context->bgd_table));
    return true;
}

static bool ext2_valid_name(const char *name, uint8_t name_len) {
    if (name == NULL || name_len == 0) {
        return false;
    }

    for (uint32_t i = 0; i < name_len; i++) {
        if (name[i] == '\0' || name[i] == '/') {
            return false;
        }
    }
    return true;
}

static bool ext2_read_inode(const struct EXT2ReadContext *context,
                            uint32_t inode_number,
                            struct EXT2Inode *inode_out) {
    struct BlockBuffer block;
    uint32_t inode_index;
    uint32_t group_index;
    uint32_t local_inode;
    uint32_t inode_table_offset;
    uint32_t inode_table_block;
    uint32_t inode_offset;

    if (context == NULL || inode_out == NULL || inode_number == 0 ||
        inode_number > context->superblock.s_inodes_count) {
        return false;
    }

    inode_index = inode_number - 1;
    group_index = inode_index / INODES_PER_GROUP;
    local_inode = inode_index % INODES_PER_GROUP;
    if (group_index >= GROUPS_COUNT) {
        return false;
    }

    inode_table_block = context->bgd_table.table[group_index].bg_inode_table;
    inode_table_offset = local_inode / INODES_PER_TABLE;
    if (inode_table_block == 0 ||
        inode_table_block >= context->superblock.s_blocks_count ||
        inode_table_block >= DISK_SPACE / BLOCK_SIZE ||
        INODES_TABLE_BLOCK_COUNT >
            context->superblock.s_blocks_count - inode_table_block ||
        INODES_TABLE_BLOCK_COUNT >
            DISK_SPACE / BLOCK_SIZE - inode_table_block ||
        inode_table_offset >= context->superblock.s_blocks_count -
                              inode_table_block ||
        inode_table_offset >= DISK_SPACE / BLOCK_SIZE - inode_table_block) {
        return false;
    }
    inode_table_block += inode_table_offset;

    inode_offset = (local_inode % INODES_PER_TABLE) * INODE_SIZE;
    if (inode_offset + INODE_SIZE > BLOCK_SIZE) {
        return false;
    }

    read_blocks(&block, inode_table_block, 1);
    memcpy(inode_out, block.buf + inode_offset, sizeof(*inode_out));
    return true;
}

static bool ext2_valid_data_block(const struct EXT2ReadContext *context,
                                  uint32_t block_number) {
    return block_number != 0 &&
           block_number < context->superblock.s_blocks_count &&
           block_number < DISK_SPACE / BLOCK_SIZE;
}

static bool ext2_read_pointer(const struct EXT2ReadContext *context,
                              uint32_t pointer_block,
                              uint32_t pointer_index,
                              uint32_t *value_out) {
    struct BlockBuffer block;

    if (value_out == NULL || pointer_index >= EXT2_POINTERS_PER_BLOCK ||
        !ext2_valid_data_block(context, pointer_block)) {
        return false;
    }

    read_blocks(&block, pointer_block, 1);
    memcpy(value_out, block.buf + pointer_index * sizeof(uint32_t),
           sizeof(*value_out));
    return true;
}

static bool ext2_get_data_block(const struct EXT2ReadContext *context,
                                const struct EXT2Inode *inode,
                                uint32_t logical_block,
                                uint32_t *block_out) {
    uint32_t block_number;

    if (context == NULL || inode == NULL || block_out == NULL ||
        logical_block >= EXT2_MAX_DATA_BLOCKS) {
        return false;
    }

    if (logical_block < EXT2_DIRECT_BLOCK_COUNT) {
        block_number = inode->i_block[logical_block];
    } else if (logical_block < EXT2_DOUBLE_INDIRECT_START) {
        if (!ext2_read_pointer(context, inode->i_block[12],
                               logical_block - EXT2_SINGLE_INDIRECT_START,
                               &block_number)) {
            return false;
        }
    } else {
        uint32_t double_index = logical_block - EXT2_DOUBLE_INDIRECT_START;
        uint32_t first_level;

        if (!ext2_read_pointer(context, inode->i_block[13],
                               double_index / EXT2_POINTERS_PER_BLOCK,
                               &first_level) ||
            !ext2_read_pointer(context, first_level,
                               double_index % EXT2_POINTERS_PER_BLOCK,
                               &block_number)) {
            return false;
        }
    }

    if (!ext2_valid_data_block(context, block_number)) {
        return false;
    }
    *block_out = block_number;
    return true;
}

static int8_t ext2_find_entry(const struct EXT2ReadContext *context,
                              uint32_t parent_inode,
                              const char *name,
                              uint8_t name_len,
                              struct EXT2DirectoryEntry *entry_out) {
    struct EXT2Inode parent;
    struct BlockBuffer directory_block;
    uint32_t directory_blocks;

    if (context == NULL || entry_out == NULL ||
        !ext2_valid_name(name, name_len)) {
        return -1;
    }
    if (parent_inode == 0 ||
        parent_inode > context->superblock.s_inodes_count) {
        return 2;
    }
    if (!ext2_read_inode(context, parent_inode, &parent)) {
        return -1;
    }
    if ((parent.i_mode & 0xF000u) != EXT2_S_IFDIR) {
        return 2;
    }

    if (parent.i_size > DISK_SPACE) {
        return -1;
    }
    directory_blocks = parent.i_size / BLOCK_SIZE;
    if (parent.i_size % BLOCK_SIZE != 0) {
        directory_blocks++;
    }
    if (directory_blocks > EXT2_MAX_DATA_BLOCKS) {
        return -1;
    }

    for (uint32_t logical_block = 0;
         logical_block < directory_blocks;
         logical_block++) {
        uint32_t disk_block;
        if (!ext2_get_data_block(context, &parent, logical_block,
                                 &disk_block)) {
            return -1;
        }
        read_blocks(&directory_block, disk_block, 1);

        uint32_t directory_offset = logical_block * BLOCK_SIZE;
        uint32_t bytes_in_block = parent.i_size - directory_offset;
        uint32_t offset = 0;
        if (bytes_in_block > BLOCK_SIZE) {
            bytes_in_block = BLOCK_SIZE;
        }
        while (offset < bytes_in_block) {
            struct EXT2DirectoryEntry entry;
            uint16_t minimum_record_length;
            uint32_t name_offset = offset + sizeof(struct EXT2DirectoryEntry);

            if (bytes_in_block - offset < sizeof(struct EXT2DirectoryEntry)) {
                return -1;
            }

            memcpy(&entry, directory_block.buf + offset, sizeof(entry));
            minimum_record_length =
                get_entry_record_len((uint8_t)entry.name_len);
            if (entry.rec_len < sizeof(struct EXT2DirectoryEntry) ||
                entry.rec_len < minimum_record_length ||
                (entry.rec_len & 3u) != 0 ||
                entry.rec_len > bytes_in_block - offset ||
                entry.name_len > UINT8_MAX ||
                name_offset + entry.name_len > offset + entry.rec_len) {
                return -1;
            }

            if (entry.inode > context->superblock.s_inodes_count) {
                return -1;
            }
            if (entry.inode != 0 && entry.name_len == name_len &&
                memcmp(directory_block.buf + name_offset, name, name_len) == 0) {
                *entry_out = entry;
                return 0;
            }

            offset += entry.rec_len;
        }
    }

    return 1;
}

char *get_entry_name(void *entry) {
    if (entry == NULL) {
        return NULL;
    }
    return (char *)((uint8_t *)entry + sizeof(struct EXT2DirectoryEntry));
}

struct EXT2DirectoryEntry *get_directory_entry(void *ptr, uint32_t offset) {
    if (ptr == NULL ||
        offset > BLOCK_SIZE - sizeof(struct EXT2DirectoryEntry)) {
        return NULL;
    }
    return (struct EXT2DirectoryEntry *)((uint8_t *)ptr + offset);
}

struct EXT2DirectoryEntry *get_next_directory_entry(
    struct EXT2DirectoryEntry *entry) {
    if (entry == NULL || entry->rec_len == 0) {
        return NULL;
    }
    return (struct EXT2DirectoryEntry *)((uint8_t *)entry + entry->rec_len);
}

uint16_t get_entry_record_len(uint8_t name_len) {
    uint16_t unaligned_length =
        (uint16_t)(sizeof(struct EXT2DirectoryEntry) + name_len);
    return (uint16_t)((unaligned_length + 3u) & ~3u);
}

uint32_t get_dir_first_child_offset(void *ptr) {
    struct EXT2DirectoryEntry dot;
    struct EXT2DirectoryEntry dotdot;
    uint32_t offset;

    if (ptr == NULL || BLOCK_SIZE < 2 * sizeof(struct EXT2DirectoryEntry)) {
        return BLOCK_SIZE;
    }

    memcpy(&dot, ptr, sizeof(dot));
    if (dot.rec_len < sizeof(dot) || dot.rec_len > BLOCK_SIZE ||
        (dot.rec_len & 3u) != 0) {
        return BLOCK_SIZE;
    }
    offset = dot.rec_len;

    if (BLOCK_SIZE - offset < sizeof(dotdot)) {
        return BLOCK_SIZE;
    }
    memcpy(&dotdot, (uint8_t *)ptr + offset, sizeof(dotdot));
    if (dotdot.rec_len < sizeof(dotdot) ||
        dotdot.rec_len > BLOCK_SIZE - offset ||
        (dotdot.rec_len & 3u) != 0) {
        return BLOCK_SIZE;
    }
    return offset + dotdot.rec_len;
}

int8_t read_directory(struct EXT2DriverRequest *request) {
    struct EXT2ReadContext context;
    struct EXT2DirectoryEntry entry;
    struct EXT2Inode parent_inode;
    struct EXT2Inode directory_inode;
    uint32_t directory_inode_number;
    uint32_t directory_blocks;
    int8_t result;

    if (request == NULL ||
        !ext2_valid_name(request->name, request->name_len)) {
        return -1;
    }
    if (!ext2_read_context_init(&context)) {
        return -1;
    }

    if (request->parent_inode == 0 ||
        request->parent_inode > context.superblock.s_inodes_count) {
        return 3;
    }
    if (!ext2_read_inode(&context, request->parent_inode, &parent_inode)) {
        return -1;
    }
    if ((parent_inode.i_mode & 0xF000u) != EXT2_S_IFDIR) {
        return 1;
    }

    if (request->name_len == 1 && request->name[0] == '.') {
        directory_inode_number = request->parent_inode;
    } else if (request->name_len == 2 &&
               request->name[0] == '.' && request->name[1] == '.') {
        struct BlockBuffer first_block;
        struct EXT2DirectoryEntry dot;
        struct EXT2DirectoryEntry dotdot;
        uint32_t first_block_number;

        if (!ext2_get_data_block(&context, &parent_inode, 0,
                                 &first_block_number)) {
            return -1;
        }
        read_blocks(&first_block, first_block_number, 1);
        memcpy(&dot, first_block.buf, sizeof(dot));
        if (dot.rec_len < sizeof(dot) ||
            dot.rec_len > BLOCK_SIZE - sizeof(dot) ||
            dot.rec_len < get_entry_record_len(1) ||
            (dot.rec_len & 3u) != 0) {
            return -1;
        }
        memcpy(&dotdot, first_block.buf + dot.rec_len, sizeof(dotdot));
        if (dot.inode != request->parent_inode ||
            dot.name_len != 1 ||
            first_block.buf[sizeof(dot)] != '.' ||
            dotdot.name_len != 2 ||
            dotdot.rec_len < get_entry_record_len(2) ||
            (dotdot.rec_len & 3u) != 0 ||
            memcmp(first_block.buf + dot.rec_len +
                       sizeof(struct EXT2DirectoryEntry),
                   "..", 2) != 0 ||
            dotdot.rec_len > BLOCK_SIZE - dot.rec_len ||
            dotdot.inode == 0 ||
            dotdot.inode > context.superblock.s_inodes_count) {
            return -1;
        }
        directory_inode_number = dotdot.inode;
    } else {
        result = ext2_find_entry(&context, request->parent_inode,
                                 request->name, request->name_len, &entry);
        if (result == 1) {
            return 2;
        }
        if (result == 2) {
            return 3;
        }
        if (result != 0) {
            return -1;
        }
        if (entry.file_type != EXT2_FT_DIR) {
            return 1;
        }
        directory_inode_number = entry.inode;
    }

    if (!ext2_read_inode(&context, directory_inode_number, &directory_inode)) {
        return -1;
    }
    if ((directory_inode.i_mode & 0xF000u) != EXT2_S_IFDIR) {
        return 1;
    }

    if (directory_inode.i_size == 0 ||
        directory_inode.i_size > DISK_SPACE) {
        return -1;
    }
    directory_blocks = directory_inode.i_size / BLOCK_SIZE;
    if (directory_inode.i_size % BLOCK_SIZE != 0) {
        directory_blocks++;
    }
    if (directory_blocks > EXT2_MAX_DATA_BLOCKS ||
        directory_blocks > request->buffer_size / BLOCK_SIZE ||
        request->buf == NULL) {
        return -1;
    }

    for (uint32_t logical_block = 0;
         logical_block < directory_blocks;
         logical_block++) {
        struct BlockBuffer block;
        uint32_t disk_block;
        if (!ext2_get_data_block(&context, &directory_inode, logical_block,
                                 &disk_block)) {
            return -1;
        }
        read_blocks(&block, disk_block, 1);
        memcpy((uint8_t *)request->buf + logical_block * BLOCK_SIZE,
               block.buf, BLOCK_SIZE);
    }

    return 0;
}

int8_t read(struct EXT2DriverRequest request) {
    struct EXT2ReadContext context;
    struct EXT2DirectoryEntry entry;
    struct EXT2Inode inode;
    int8_t result;
    uint32_t bytes_read = 0;

    if (!ext2_valid_name(request.name, request.name_len)) {
        return -1;
    }
    if (!ext2_read_context_init(&context)) {
        return -1;
    }

    result = ext2_find_entry(&context, request.parent_inode, request.name,
                             request.name_len, &entry);
    if (result == 1) {
        return 3;
    }
    if (result == 2) {
        return 4;
    }
    if (result != 0) {
        return -1;
    }
    if (entry.file_type != EXT2_FT_REG_FILE) {
        return 1;
    }
    if (!ext2_read_inode(&context, entry.inode, &inode)) {
        return -1;
    }
    if ((inode.i_mode & 0xF000u) != EXT2_S_IFREG) {
        return 1;
    }
    if (request.buffer_size < inode.i_size) {
        return 2;
    }
    if (inode.i_size != 0 && request.buf == NULL) {
        return -1;
    }
    if (inode.i_size > DISK_SPACE ||
        inode.i_size / BLOCK_SIZE +
            (inode.i_size % BLOCK_SIZE != 0) > EXT2_MAX_DATA_BLOCKS) {
        return -1;
    }

    while (bytes_read < inode.i_size) {
        struct BlockBuffer data_block;
        uint32_t logical_block = bytes_read / BLOCK_SIZE;
        uint32_t block_number;
        uint32_t bytes_to_copy = inode.i_size - bytes_read;

        if (bytes_to_copy > BLOCK_SIZE) {
            bytes_to_copy = BLOCK_SIZE;
        }
        if (!ext2_get_data_block(&context, &inode, logical_block,
                                 &block_number)) {
            return -1;
        }
        read_blocks(&data_block, block_number, 1);
        memcpy((uint8_t *)request.buf + bytes_read, data_block.buf,
               bytes_to_copy);
        bytes_read += bytes_to_copy;
    }

    return 0;
}
