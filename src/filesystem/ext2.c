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

/* Cached filesystem metadata used after initialization. */
static struct EXT2Superblock superblock;
static struct EXT2BlockGroupDescriptorTable bgd_table;

#define EXT2_ROOT_INODE 1u
#define EXT2_SUPERBLOCK_BLOCK 1u
#define EXT2_BGD_TABLE_BLOCK 2u
#define EXT2_FIRST_METADATA_BLOCK 3u
#define EXT2_METADATA_BLOCKS_PER_GROUP \
    (2u + INODES_TABLE_BLOCK_COUNT)
#define EXT2_TOTAL_BLOCKS (DISK_SPACE / BLOCK_SIZE)
/* Data starts after the signature, superblock, BGD, and group metadata. */
#define EXT2_FIRST_DATA_BLOCK \
    (EXT2_FIRST_METADATA_BLOCK + \
     GROUPS_COUNT * EXT2_METADATA_BLOCKS_PER_GROUP)

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

static void ext2_set_bitmap_bit(struct BlockBuffer *bitmap,
                                uint32_t index) {
    bitmap->buf[index / 8u] |= (uint8_t)(1u << (index % 8u));
}

uint32_t inode_to_bgd(uint32_t inode) {
    /* Inode numbering starts from 1, while array indexes start from 0. */
    return (inode - 1u) / INODES_PER_GROUP;
}

uint32_t inode_to_local(uint32_t inode) {
    return (inode - 1u) % INODES_PER_GROUP;
}

void sync_node(struct EXT2Inode *node, uint32_t inode) {
    struct BlockBuffer inode_table_block;
    uint32_t bgd_index;
    uint32_t local_index;
    uint32_t table_block;
    uint32_t table_offset;

    if (node == NULL || inode == 0 ||
        inode > INODES_PER_GROUP * GROUPS_COUNT) {
        return;
    }

    bgd_index = inode_to_bgd(inode);
    local_index = inode_to_local(inode);
    table_block = bgd_table.table[bgd_index].bg_inode_table +
                  local_index / INODES_PER_TABLE;
    table_offset = (local_index % INODES_PER_TABLE) * INODE_SIZE;

    read_blocks(&inode_table_block, table_block, 1);
    memcpy(inode_table_block.buf + table_offset, node, sizeof(*node));
    write_blocks(&inode_table_block, table_block, 1);
}

void init_directory_table(struct EXT2Inode *node, uint32_t inode,
                          uint32_t parent_inode) {
    struct BlockBuffer directory_block;
    struct EXT2DirectoryEntry entry;
    uint32_t offset = 0;

    if (node == NULL || node->i_block[0] == 0 || inode == 0 ||
        parent_inode == 0) {
        return;
    }

    memset(&directory_block, 0, sizeof(directory_block));

    memset(&entry, 0, sizeof(entry));
    entry.inode = inode;
    entry.rec_len = get_entry_record_len(1);
    entry.name_len = 1;
    entry.file_type = EXT2_FT_DIR;
    memcpy(directory_block.buf + offset, &entry, sizeof(entry));
    directory_block.buf[offset + sizeof(entry)] = '.';
    offset += entry.rec_len;

    memset(&entry, 0, sizeof(entry));
    entry.inode = parent_inode;
    entry.rec_len = get_entry_record_len(2);
    entry.name_len = 2;
    entry.file_type = EXT2_FT_DIR;
    memcpy(directory_block.buf + offset, &entry, sizeof(entry));
    memcpy(directory_block.buf + offset + sizeof(entry), "..", 2);
    offset += entry.rec_len;

    memset(&entry, 0, sizeof(entry));
    /* The unused record fills the rest of the block for future children. */
    entry.rec_len = (uint16_t)(BLOCK_SIZE - offset);
    memcpy(directory_block.buf + offset, &entry, sizeof(entry));

    node->i_mode = EXT2_S_IFDIR;
    node->i_size = BLOCK_SIZE;
    node->i_blocks = 1;
    write_blocks(&directory_block, node->i_block[0], 1);
}

bool is_empty_storage(void) {
    struct BlockBuffer boot_sector;

    read_blocks(&boot_sector, BOOT_SECTOR, 1);
    return memcmp(boot_sector.buf, fs_signature, BLOCK_SIZE) != 0;
}

void create_ext2(void) {
    struct BlockBuffer block;
    struct EXT2Inode root_inode;
    uint32_t used_blocks = EXT2_FIRST_DATA_BLOCK + 1u;

    memset(&superblock, 0, sizeof(superblock));
    superblock.s_inodes_count = INODES_PER_GROUP * GROUPS_COUNT;
    superblock.s_blocks_count = EXT2_TOTAL_BLOCKS;
    superblock.s_free_blocks_count = EXT2_TOTAL_BLOCKS - used_blocks;
    superblock.s_free_inodes_count = superblock.s_inodes_count - 1u;
    superblock.s_first_data_block = 0;
    superblock.s_first_ino = EXT2_ROOT_INODE + 1u;
    superblock.s_blocks_per_group = BLOCKS_PER_GROUP;
    superblock.s_frags_per_group = BLOCKS_PER_GROUP;
    superblock.s_inodes_per_group = INODES_PER_GROUP;
    superblock.s_magic = EXT2_SUPER_MAGIC;

    memset(&bgd_table, 0, sizeof(bgd_table));
    for (uint32_t group = 0; group < GROUPS_COUNT; group++) {
        struct EXT2BlockGroupDescriptor *descriptor =
            &bgd_table.table[group];
        uint32_t metadata_start = EXT2_FIRST_METADATA_BLOCK +
                                  group * EXT2_METADATA_BLOCKS_PER_GROUP;
        uint32_t group_start = group * BLOCKS_PER_GROUP;
        uint32_t group_end = group_start + BLOCKS_PER_GROUP;
        uint32_t group_used_blocks = 0;

        /* Each group owns one block bitmap, one inode bitmap, and an inode table. */
        descriptor->bg_block_bitmap = metadata_start;
        descriptor->bg_inode_bitmap = metadata_start + 1u;
        descriptor->bg_inode_table = metadata_start + 2u;

        if (used_blocks > group_start) {
            uint32_t used_end = used_blocks < group_end ?
                                used_blocks : group_end;
            group_used_blocks = used_end - group_start;
        }
        descriptor->bg_free_blocks_count =
            (uint16_t)(BLOCKS_PER_GROUP - group_used_blocks);
        descriptor->bg_free_inodes_count = INODES_PER_GROUP;
    }
    bgd_table.table[0].bg_free_inodes_count--;
    bgd_table.table[0].bg_used_dirs_count = 1;

    /* Initialize allocation bitmaps and inode tables for every group. */
    for (uint32_t group = 0; group < GROUPS_COUNT; group++) {
        const struct EXT2BlockGroupDescriptor *descriptor =
            &bgd_table.table[group];
        uint32_t group_start = group * BLOCKS_PER_GROUP;

        memset(&block, 0, sizeof(block));
        /* A block bitmap bit represents an offset inside its block group. */
        for (uint32_t absolute_block = group_start;
             absolute_block < used_blocks &&
             absolute_block < group_start + BLOCKS_PER_GROUP;
             absolute_block++) {
            ext2_set_bitmap_bit(&block, absolute_block - group_start);
        }
        write_blocks(&block, descriptor->bg_block_bitmap, 1);

        memset(&block, 0, sizeof(block));
        if (group == 0) {
            ext2_set_bitmap_bit(&block, EXT2_ROOT_INODE - 1u);
        }
        write_blocks(&block, descriptor->bg_inode_bitmap, 1);

        memset(&block, 0, sizeof(block));
        for (uint32_t table_index = 0;
             table_index < INODES_TABLE_BLOCK_COUNT;
             table_index++) {
            write_blocks(&block, descriptor->bg_inode_table + table_index, 1);
        }
    }

    memset(&root_inode, 0, sizeof(root_inode));
    root_inode.i_block[0] = EXT2_FIRST_DATA_BLOCK;
    init_directory_table(&root_inode, EXT2_ROOT_INODE, EXT2_ROOT_INODE);
    sync_node(&root_inode, EXT2_ROOT_INODE);

    memset(&block, 0, sizeof(block));
    memcpy(block.buf, &superblock, sizeof(superblock));
    write_blocks(&block, EXT2_SUPERBLOCK_BLOCK, 1);

    memset(&block, 0, sizeof(block));
    memcpy(block.buf, &bgd_table, sizeof(bgd_table));
    write_blocks(&block, EXT2_BGD_TABLE_BLOCK, 1);

    /* Write the signature last so an interrupted format is retried. */
    write_blocks(fs_signature, BOOT_SECTOR, 1);
}

void initialize_filesystem_ext2(void) {
    struct BlockBuffer block;

    if (is_empty_storage()) {
        create_ext2();
        return;
    }

    read_blocks(&block, EXT2_SUPERBLOCK_BLOCK, 1);
    memcpy(&superblock, block.buf, sizeof(superblock));
    read_blocks(&block, EXT2_BGD_TABLE_BLOCK, 1);
    memcpy(&bgd_table, block.buf, sizeof(bgd_table));
}

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
                              uint8_t wanted_file_type,
                              struct EXT2DirectoryEntry *entry_out) {
    struct EXT2Inode parent;
    struct BlockBuffer directory_block;
    uint32_t directory_blocks;
    bool name_exists = false;

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
            if (entry.name_len > UINT8_MAX) {
                return -1;
            }
            minimum_record_length =
                get_entry_record_len((uint8_t)entry.name_len);
            if (entry.rec_len < sizeof(struct EXT2DirectoryEntry) ||
                entry.rec_len < minimum_record_length ||
                (entry.rec_len & 3u) != 0 ||
                entry.rec_len > bytes_in_block - offset ||
                name_offset + entry.name_len > offset + entry.rec_len) {
                return -1;
            }

            if (entry.inode > context->superblock.s_inodes_count) {
                return -1;
            }
            if (entry.inode != 0 && entry.name_len == name_len &&
                memcmp(directory_block.buf + name_offset, name, name_len) == 0) {
                name_exists = true;
                if (wanted_file_type == EXT2_FT_UNKNOWN ||
                    entry.file_type == wanted_file_type) {
                    *entry_out = entry;
                    return 0;
                }
            }

            offset += entry.rec_len;
        }
    }

    return name_exists ? 3 : 1;
}

static bool ext2_read_cached_inode(uint32_t inode_number,
                                   struct EXT2Inode *inode_out) {
    struct BlockBuffer block;
    uint32_t group;
    uint32_t local;
    uint32_t table_block;
    uint32_t offset;

    if (inode_out == NULL || inode_number == 0 ||
        inode_number > superblock.s_inodes_count) {
        return false;
    }

    group = inode_to_bgd(inode_number);
    local = inode_to_local(inode_number);
    if (group >= GROUPS_COUNT) {
        return false;
    }

    table_block = bgd_table.table[group].bg_inode_table +
                  local / INODES_PER_TABLE;
    offset = (local % INODES_PER_TABLE) * INODE_SIZE;
    if (table_block >= superblock.s_blocks_count ||
        offset + INODE_SIZE > BLOCK_SIZE) {
        return false;
    }

    read_blocks(&block, table_block, 1);
    memcpy(inode_out, block.buf + offset, sizeof(*inode_out));
    return true;
}

static void ext2_sync_metadata(void) {
    struct BlockBuffer block;

    memset(&block, 0, sizeof(block));
    memcpy(block.buf, &superblock, sizeof(superblock));
    write_blocks(&block, EXT2_SUPERBLOCK_BLOCK, 1);

    memset(&block, 0, sizeof(block));
    memcpy(block.buf, &bgd_table, sizeof(bgd_table));
    write_blocks(&block, EXT2_BGD_TABLE_BLOCK, 1);
}

static bool ext2_is_metadata_block(uint32_t block_number) {
    if (block_number <= EXT2_BGD_TABLE_BLOCK) {
        return true;
    }

    for (uint32_t group = 0; group < GROUPS_COUNT; group++) {
        const struct EXT2BlockGroupDescriptor *descriptor =
            &bgd_table.table[group];
        if (block_number == descriptor->bg_block_bitmap ||
            block_number == descriptor->bg_inode_bitmap ||
            (block_number >= descriptor->bg_inode_table &&
             block_number < descriptor->bg_inode_table +
                            INODES_TABLE_BLOCK_COUNT)) {
            return true;
        }
    }
    return false;
}

static bool ext2_release_physical_block(uint32_t block_number,
                                        struct BlockBuffer *bitmap,
                                        uint32_t *last_bgd) {
    uint32_t blocks_per_group = superblock.s_blocks_per_group;
    uint32_t group;
    uint32_t local;
    uint8_t mask;

    if (bitmap == NULL || blocks_per_group == 0 || block_number == 0 ||
        block_number >= superblock.s_blocks_count ||
        ext2_is_metadata_block(block_number)) {
        return false;
    }

    group = block_number / blocks_per_group;
    local = block_number % blocks_per_group;
    if (group >= GROUPS_COUNT || local >= BLOCK_SIZE * 8u) {
        return false;
    }

    read_blocks(bitmap, bgd_table.table[group].bg_block_bitmap, 1);
    mask = (uint8_t)(1u << (local % 8u));
    if ((bitmap->buf[local / 8u] & mask) == 0) {
        return false;
    }

    bitmap->buf[local / 8u] &= (uint8_t)~mask;
    write_blocks(bitmap, bgd_table.table[group].bg_block_bitmap, 1);
    bgd_table.table[group].bg_free_blocks_count++;
    superblock.s_free_blocks_count++;
    if (last_bgd != NULL) {
        *last_bgd = group;
    }
    return true;
}

static bool ext2_release_block(uint32_t block_number) {
    struct BlockBuffer bitmap;
    uint32_t last_bgd = 0;

    return ext2_release_physical_block(block_number, &bitmap, &last_bgd);
}

/**
 * @brief Find and reserve the first free block in ascending disk order.
 * The block bitmap and both free-block counters are updated in memory.
 * The caller must persist the counters using ext2_sync_metadata().
 */
static bool ext2_allocate_physical_block(uint32_t preferred_bgd,
                                         uint32_t *block_out) {
    uint32_t blocks_per_group = superblock.s_blocks_per_group;

    /*
     * Preserve the parameter for compatibility with existing callers.
     * Block allocation follows global First Fit order.
     */
    (void)preferred_bgd;

    if (block_out == NULL || blocks_per_group == 0 ||
        blocks_per_group > BLOCK_SIZE * 8u ||
        superblock.s_free_blocks_count == 0) {
        return false;
    }

    for (uint32_t group = 0; group < GROUPS_COUNT; group++) {
        struct EXT2BlockGroupDescriptor *descriptor =
            &bgd_table.table[group];
        struct BlockBuffer bitmap;
        uint32_t group_start = group * blocks_per_group;
        uint32_t block_count;

        if (descriptor->bg_free_blocks_count == 0 ||
            group_start >= superblock.s_blocks_count ||
            descriptor->bg_block_bitmap == 0 ||
            descriptor->bg_block_bitmap >= superblock.s_blocks_count ||
            descriptor->bg_block_bitmap >= DISK_SPACE / BLOCK_SIZE) {
            continue;
        }

        block_count = superblock.s_blocks_count - group_start;
        if (block_count > blocks_per_group) {
            block_count = blocks_per_group;
        }
        if (block_count > BLOCK_SIZE * 8u) {
            block_count = BLOCK_SIZE * 8u;
        }

        read_blocks(&bitmap, descriptor->bg_block_bitmap, 1);
        for (uint32_t local = 0; local < block_count; local++) {
            uint32_t block_number = group_start + local;

            if (block_number < EXT2_FIRST_DATA_BLOCK ||
                block_number >= superblock.s_blocks_count ||
                ext2_is_metadata_block(block_number)) {
                continue;
            }
            if ((bitmap.buf[local / 8u] &
                 (uint8_t)(1u << (local % 8u))) != 0) {
                continue;
            }

            ext2_set_bitmap_bit(&bitmap, local);
            write_blocks(&bitmap, descriptor->bg_block_bitmap, 1);
            descriptor->bg_free_blocks_count--;
            superblock.s_free_blocks_count--;
            *block_out = block_number;
            return true;
        }
    }

    return false;
}

static bool ext2_ensure_pointer_block(uint32_t *pointer,
                                      uint32_t preferred_bgd,
                                      uint32_t *new_blocks) {
    struct BlockBuffer block;
    uint32_t block_number;

    if (pointer == NULL || new_blocks == NULL) {
        return false;
    }
    if (*pointer != 0) {
        return true;
    }
    if (!ext2_allocate_physical_block(preferred_bgd, &block_number)) {
        return false;
    }

    memset(&block, 0, sizeof(block));
    write_blocks(&block, block_number, 1);
    *pointer = block_number;
    (*new_blocks)++;
    return true;
}

/**
 * @brief Store data_block at the requested logical position.
 * Missing indirect pointer blocks are allocated and reported through
 * new_pointer_blocks. On failure all newly created pointer blocks are
 * released, so the inode is left unchanged.
 */
static bool ext2_set_data_block_pointer(struct EXT2Inode *node,
                                        uint32_t logical_block,
                                        uint32_t data_block,
                                        uint32_t preferred_bgd,
                                        uint32_t *new_pointer_blocks) {
    struct BlockBuffer pointer_block;

    if (node == NULL || new_pointer_blocks == NULL ||
        logical_block >= EXT2_MAX_DATA_BLOCKS) {
        return false;
    }
    *new_pointer_blocks = 0;

    if (logical_block < EXT2_DIRECT_BLOCK_COUNT) {
        node->i_block[logical_block] = data_block;
        return true;
    }

    if (logical_block < EXT2_DOUBLE_INDIRECT_START) {
        uint32_t created = 0;
        /* local copy: i_block lives in a packed struct, so no pointer into it */
        uint32_t single_pointer = node->i_block[12];

        if (!ext2_ensure_pointer_block(&single_pointer, preferred_bgd,
                                       &created)) {
            return false;
        }
        node->i_block[12] = single_pointer;
        read_blocks(&pointer_block, node->i_block[12], 1);
        memcpy(pointer_block.buf +
                   (logical_block - EXT2_SINGLE_INDIRECT_START) *
                       sizeof(uint32_t),
               &data_block, sizeof(data_block));
        write_blocks(&pointer_block, node->i_block[12], 1);
        *new_pointer_blocks = created;
        return true;
    }

    {
        uint32_t double_index = logical_block - EXT2_DOUBLE_INDIRECT_START;
        uint32_t first_index = double_index / EXT2_POINTERS_PER_BLOCK;
        uint32_t second_index = double_index % EXT2_POINTERS_PER_BLOCK;
        uint32_t double_created = 0;
        uint32_t first_created = 0;
        uint32_t first_pointer = 0;
        uint32_t double_pointer = node->i_block[13];

        if (!ext2_ensure_pointer_block(&double_pointer, preferred_bgd,
                                       &double_created)) {
            return false;
        }
        node->i_block[13] = double_pointer;
        read_blocks(&pointer_block, node->i_block[13], 1);
        memcpy(&first_pointer,
               pointer_block.buf + first_index * sizeof(uint32_t),
               sizeof(first_pointer));

        if (first_pointer == 0) {
            struct BlockBuffer first_block;

            if (!ext2_allocate_physical_block(preferred_bgd,
                                              &first_pointer)) {
                if (double_created) {
                    ext2_release_block(node->i_block[13]);
                    node->i_block[13] = 0;
                }
                return false;
            }
            memset(&first_block, 0, sizeof(first_block));
            write_blocks(&first_block, first_pointer, 1);
            first_created = 1;
            memcpy(pointer_block.buf + first_index * sizeof(uint32_t),
                   &first_pointer, sizeof(first_pointer));
            write_blocks(&pointer_block, node->i_block[13], 1);
        }

        read_blocks(&pointer_block, first_pointer, 1);
        memcpy(pointer_block.buf + second_index * sizeof(uint32_t),
               &data_block, sizeof(data_block));
        write_blocks(&pointer_block, first_pointer, 1);
        *new_pointer_blocks = double_created + first_created;
    }

    return true;
}

/**
 * @brief Undo a partially built inode block map created by
 * ext2_allocate_node_blocks_checked().
 * @param node freshly zeroed inode that owns the blocks
 * @param data_blocks number of data blocks successfully linked
 */
static void ext2_rollback_node_blocks(struct EXT2Inode *node,
                                      uint32_t data_blocks) {
    if (node == NULL) {
        return;
    }

    deallocate_blocks(node->i_block, data_blocks);

    /* Pointer block may exist before the first indirect data block. */
    if (node->i_block[12] != 0 &&
        data_blocks <= EXT2_DIRECT_BLOCK_COUNT) {
        ext2_release_block(node->i_block[12]);
    }
    if (node->i_block[13] != 0 &&
        data_blocks <= EXT2_DOUBLE_INDIRECT_START) {
        struct BlockBuffer double_block;

        if (node->i_block[13] < superblock.s_blocks_count) {
            read_blocks(&double_block, node->i_block[13], 1);
            for (uint32_t i = 0; i < EXT2_POINTERS_PER_BLOCK; i++) {
                uint32_t first_pointer;
                memcpy(&first_pointer,
                       double_block.buf + i * sizeof(uint32_t),
                       sizeof(first_pointer));
                if (first_pointer != 0) {
                    ext2_release_block(first_pointer);
                }
            }
        }
        ext2_release_block(node->i_block[13]);
    }

    memset(node->i_block, 0, sizeof(node->i_block));
    node->i_blocks = 0;
    ext2_sync_metadata();
}

/**
 * @brief Allocate and write every data block of a new inode.
 * Supports direct, single indirect, and double indirect blocks.
 * @return false after rolling back all blocks allocated by this call
 */
static bool ext2_allocate_node_blocks_checked(void *ptr,
                                              struct EXT2Inode *node,
                                              uint32_t preferred_bgd) {
    uint32_t data_blocks;
    uint32_t allocated_blocks = 0;
    uint32_t pointer_blocks = 0;

    if (node == NULL) {
        return false;
    }
    if (node->i_size == 0) {
        node->i_blocks = 0;
        return true;
    }
    if (ptr == NULL ||
        node->i_size > EXT2_MAX_DATA_BLOCKS * BLOCK_SIZE) {
        return false;
    }

    data_blocks = (node->i_size + BLOCK_SIZE - 1u) / BLOCK_SIZE;
    for (uint32_t logical_block = 0;
         logical_block < data_blocks;
         logical_block++) {
        struct BlockBuffer data_block;
        uint32_t block_number;
        uint32_t bytes = node->i_size - logical_block * BLOCK_SIZE;
        uint32_t created_pointers = 0;

        if (bytes > BLOCK_SIZE) {
            bytes = BLOCK_SIZE;
        }
        if (!ext2_allocate_physical_block(preferred_bgd, &block_number)) {
            ext2_rollback_node_blocks(node, allocated_blocks);
            return false;
        }
        if (!ext2_set_data_block_pointer(node, logical_block, block_number,
                                         preferred_bgd,
                                         &created_pointers)) {
            ext2_release_block(block_number);
            ext2_rollback_node_blocks(node, allocated_blocks);
            return false;
        }
        allocated_blocks++;
        pointer_blocks += created_pointers;

        memset(&data_block, 0, sizeof(data_block));
        memcpy(data_block.buf,
               (uint8_t *)ptr + logical_block * BLOCK_SIZE, bytes);
        write_blocks(&data_block, block_number, 1);
    }

    node->i_blocks = allocated_blocks + pointer_blocks;
    return true;
}

void allocate_node_blocks(void *ptr, struct EXT2Inode *node,
                          uint32_t prefered_bgd) {
    (void)ext2_allocate_node_blocks_checked(ptr, node, prefered_bgd);
}

uint32_t allocate_node(void) {
    struct BlockBuffer inode_bitmap;

    if (superblock.s_free_inodes_count == 0 ||
        superblock.s_inodes_per_group == 0 ||
        superblock.s_inodes_per_group > INODES_PER_GROUP) {
        return 0;
    }

    for (uint32_t group = 0; group < GROUPS_COUNT; group++) {
        struct EXT2BlockGroupDescriptor *descriptor =
            &bgd_table.table[group];
        uint32_t first_inode = group * INODES_PER_GROUP + 1u;
        uint32_t inode_count;

        if (descriptor->bg_free_inodes_count == 0 ||
            first_inode > superblock.s_inodes_count ||
            descriptor->bg_inode_bitmap == 0 ||
            descriptor->bg_inode_bitmap >= superblock.s_blocks_count ||
            descriptor->bg_inode_bitmap >= DISK_SPACE / BLOCK_SIZE) {
            continue;
        }

        inode_count = superblock.s_inodes_count - first_inode + 1u;
        if (inode_count > INODES_PER_GROUP) {
            inode_count = INODES_PER_GROUP;
        }
        if (inode_count > BLOCK_SIZE * 8u) {
            inode_count = BLOCK_SIZE * 8u;
        }

        read_blocks(&inode_bitmap, descriptor->bg_inode_bitmap, 1);
        for (uint32_t local = 0; local < inode_count; local++) {
            uint32_t inode_number = first_inode + local;

            if (inode_number <= EXT2_ROOT_INODE ||
                (inode_bitmap.buf[local / 8u] &
                 (uint8_t)(1u << (local % 8u))) != 0) {
                continue;
            }

            ext2_set_bitmap_bit(&inode_bitmap, local);
            write_blocks(&inode_bitmap, descriptor->bg_inode_bitmap, 1);
            descriptor->bg_free_inodes_count--;
            superblock.s_free_inodes_count--;
            ext2_sync_metadata();
            return inode_number;
        }
    }

    return 0;
}

uint32_t deallocate_block(uint32_t *locations, uint32_t blocks,
                          struct BlockBuffer *bitmap, uint32_t depth,
                          uint32_t *last_bgd, bool bgd_loaded) {
    uint32_t capacity = 1;
    uint32_t location_count;
    uint32_t remaining = blocks;

    (void)bgd_loaded;
    if (locations == NULL || bitmap == NULL || blocks == 0) {
        return last_bgd == NULL ? 0 : *last_bgd;
    }

    for (uint32_t level = 0; level < depth; level++) {
        capacity *= EXT2_POINTERS_PER_BLOCK;
    }
    location_count = (blocks + capacity - 1u) / capacity;

    for (uint32_t i = 0; i < location_count && remaining > 0; i++) {
        uint32_t block_number = locations[i];
        uint32_t child_count = remaining < capacity ? remaining : capacity;

        if (block_number != 0) {
            if (depth == 0) {
                ext2_release_physical_block(block_number, bitmap, last_bgd);
            } else {
                struct BlockBuffer pointer_block;
                read_blocks(&pointer_block, block_number, 1);
                deallocate_block((uint32_t *)pointer_block.buf, child_count,
                                 bitmap, depth - 1u, last_bgd, true);
                ext2_release_physical_block(block_number, bitmap, last_bgd);
            }
        }
        remaining -= child_count;
    }

    return last_bgd == NULL ? 0 : *last_bgd;
}

void deallocate_blocks(void *loc, uint32_t blocks) {
    uint32_t *locations = loc;
    uint32_t remaining = blocks;
    uint32_t count;
    uint32_t last_bgd = 0;
    struct BlockBuffer bitmap;

    if (locations == NULL || blocks == 0) {
        return;
    }

    count = remaining < EXT2_DIRECT_BLOCK_COUNT ?
            remaining : EXT2_DIRECT_BLOCK_COUNT;
    deallocate_block(locations, count, &bitmap, 0, &last_bgd, false);
    remaining -= count;

    if (remaining > 0) {
        count = remaining < EXT2_POINTERS_PER_BLOCK ?
                remaining : EXT2_POINTERS_PER_BLOCK;
        deallocate_block(&locations[12], count, &bitmap, 1,
                         &last_bgd, false);
        remaining -= count;
    }

    if (remaining > 0) {
        deallocate_block(&locations[13], remaining, &bitmap, 2,
                         &last_bgd, false);
    }
}

void deallocate_node(uint32_t inode_number) {
    struct EXT2Inode inode;
    struct EXT2Inode empty_inode;
    struct BlockBuffer inode_bitmap;
    uint32_t group;
    uint32_t local;
    uint32_t data_blocks;
    uint8_t mask;
    bool is_directory;

    /* Inode 1 is the root directory and must never be released. */
    if (inode_number <= EXT2_ROOT_INODE ||
        inode_number > superblock.s_inodes_count ||
        !ext2_read_cached_inode(inode_number, &inode)) {
        return;
    }

    group = inode_to_bgd(inode_number);
    local = inode_to_local(inode_number);
    read_blocks(&inode_bitmap, bgd_table.table[group].bg_inode_bitmap, 1);
    mask = (uint8_t)(1u << (local % 8u));
    if ((inode_bitmap.buf[local / 8u] & mask) == 0) {
        return;
    }

    data_blocks = inode.i_size / BLOCK_SIZE;
    if (inode.i_size % BLOCK_SIZE != 0) {
        data_blocks++;
    }
    deallocate_blocks(inode.i_block, data_blocks);

    is_directory = (inode.i_mode & 0xF000u) == EXT2_S_IFDIR;
    memset(&empty_inode, 0, sizeof(empty_inode));
    sync_node(&empty_inode, inode_number);

    inode_bitmap.buf[local / 8u] &= (uint8_t)~mask;
    write_blocks(&inode_bitmap, bgd_table.table[group].bg_inode_bitmap, 1);
    bgd_table.table[group].bg_free_inodes_count++;
    superblock.s_free_inodes_count++;
    if (is_directory && bgd_table.table[group].bg_used_dirs_count > 0) {
        bgd_table.table[group].bg_used_dirs_count--;
    }
    ext2_sync_metadata();
}

static bool ext2_directory_is_empty(const struct EXT2ReadContext *context,
                                    uint32_t inode_number) {
    struct EXT2Inode inode;
    uint32_t block_count;
    uint32_t entry_index = 0;

    if (!ext2_read_inode(context, inode_number, &inode) ||
        (inode.i_mode & 0xF000u) != EXT2_S_IFDIR ||
        inode.i_size == 0 || inode.i_size > DISK_SPACE) {
        return false;
    }

    block_count = (inode.i_size + BLOCK_SIZE - 1u) / BLOCK_SIZE;
    for (uint32_t logical = 0; logical < block_count; logical++) {
        struct BlockBuffer block;
        uint32_t disk_block;
        uint32_t bytes = inode.i_size - logical * BLOCK_SIZE;
        uint32_t offset = 0;

        if (bytes > BLOCK_SIZE) {
            bytes = BLOCK_SIZE;
        }
        if (!ext2_get_data_block(context, &inode, logical, &disk_block)) {
            return false;
        }
        read_blocks(&block, disk_block, 1);

        while (offset < bytes) {
            struct EXT2DirectoryEntry entry;
            const char *name;
            bool is_dot;
            bool is_dotdot;

            if (bytes - offset < sizeof(entry)) {
                return false;
            }
            memcpy(&entry, block.buf + offset, sizeof(entry));
            if (entry.rec_len < sizeof(entry) ||
                (entry.rec_len & 3u) != 0 ||
                entry.rec_len > bytes - offset ||
                entry.rec_len < get_entry_record_len((uint8_t)entry.name_len) ||
                entry.name_len > UINT8_MAX) {
                return false;
            }

            name = (const char *)(block.buf + offset + sizeof(entry));
            is_dot = entry.name_len == 1 && name[0] == '.';
            is_dotdot = entry.name_len == 2 &&
                        name[0] == '.' && name[1] == '.';
            if (entry_index == 0 &&
                (entry.inode != inode_number || !is_dot)) {
                return false;
            }
            if (entry_index == 1 &&
                (entry.inode == 0 || !is_dotdot)) {
                return false;
            }
            if (entry_index > 1 && entry.inode != 0) {
                return false;
            }
            entry_index++;
            offset += entry.rec_len;
        }
    }
    return entry_index >= 2;
}

bool is_directory_empty(uint32_t inode) {
    struct EXT2ReadContext context;

    return ext2_read_context_init(&context) &&
           ext2_directory_is_empty(&context, inode);
}

static bool ext2_unlink_entry(const struct EXT2ReadContext *context,
                              uint32_t parent_inode_number,
                              const char *name, uint8_t name_len,
                              uint32_t target_inode) {
    struct EXT2Inode parent;
    uint32_t block_count;

    if (!ext2_read_inode(context, parent_inode_number, &parent) ||
        (parent.i_mode & 0xF000u) != EXT2_S_IFDIR) {
        return false;
    }

    block_count = (parent.i_size + BLOCK_SIZE - 1u) / BLOCK_SIZE;
    for (uint32_t logical = 0; logical < block_count; logical++) {
        struct BlockBuffer block;
        uint32_t disk_block;
        uint32_t bytes = parent.i_size - logical * BLOCK_SIZE;
        uint32_t offset = 0;

        if (bytes > BLOCK_SIZE) {
            bytes = BLOCK_SIZE;
        }
        if (!ext2_get_data_block(context, &parent, logical, &disk_block)) {
            return false;
        }
        read_blocks(&block, disk_block, 1);

        while (offset < bytes) {
            struct EXT2DirectoryEntry entry;
            if (bytes - offset < sizeof(entry)) {
                return false;
            }
            memcpy(&entry, block.buf + offset, sizeof(entry));
            if (entry.rec_len < sizeof(entry) ||
                (entry.rec_len & 3u) != 0 ||
                entry.rec_len > bytes - offset ||
                entry.name_len > UINT8_MAX ||
                entry.rec_len < get_entry_record_len((uint8_t)entry.name_len)) {
                return false;
            }

            if (entry.inode == target_inode && entry.name_len == name_len &&
                memcmp(block.buf + offset + sizeof(entry), name, name_len) == 0) {
                entry.inode = 0;
                memcpy(block.buf + offset, &entry, sizeof(entry));
                write_blocks(&block, disk_block, 1);
                return true;
            }
            offset += entry.rec_len;
        }
    }
    return false;
}

int8_t delete(struct EXT2DriverRequest request) {
    struct EXT2ReadContext context;
    struct EXT2DirectoryEntry entry;
    struct EXT2Inode target;
    int8_t result;
    bool target_is_directory;

    if (!ext2_valid_name(request.name, request.name_len) ||
        (request.name_len == 1 && request.name[0] == '.') ||
        (request.name_len == 2 && request.name[0] == '.' &&
         request.name[1] == '.')) {
        return -1;
    }
    if (!ext2_read_context_init(&context)) {
        return -1;
    }

    /*
     * File and directory entries may share a name.  Select the requested
     * type during lookup instead of unlinking whichever matching entry was
     * encountered first in the directory table.
     */
    result = ext2_find_entry(
        &context, request.parent_inode, request.name, request.name_len,
        request.is_directory ? EXT2_FT_DIR : EXT2_FT_REG_FILE, &entry);
    if (result == 1) {
        return 1;
    }
    if (result == 3) {
        /* A same-named entry of the other type is not this delete target. */
        return 1;
    }
    if (result == 2) {
        return 3;
    }
    if (result != 0 || entry.inode == EXT2_ROOT_INODE ||
        !ext2_read_inode(&context, entry.inode, &target)) {
        return -1;
    }

    if (entry.file_type != EXT2_FT_DIR &&
        entry.file_type != EXT2_FT_REG_FILE) {
        return -1;
    }
    target_is_directory = entry.file_type == EXT2_FT_DIR;
    if (target_is_directory != request.is_directory ||
        (target_is_directory &&
         (target.i_mode & 0xF000u) != EXT2_S_IFDIR) ||
        (!target_is_directory &&
         (target.i_mode & 0xF000u) != EXT2_S_IFREG)) {
        return 1;
    }
    if (target_is_directory &&
        !ext2_directory_is_empty(&context, entry.inode)) {
        return 2;
    }

    if (!ext2_unlink_entry(&context, request.parent_inode, request.name,
                           request.name_len, entry.inode)) {
        return -1;
    }
    deallocate_node(entry.inode);
    return 0;
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
        return 3;
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
                                 request->name, request->name_len,
                                 EXT2_FT_DIR, &entry);
        if (result == 1) {
            return 2;
        }
        if (result == 2) {
            return 3;
        }
        if (result == 3) {
            return 1;
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

static void ext2_fill_directory_entry(void *destination, uint16_t rec_len,
                                      uint32_t inode, const char *name,
                                      uint8_t name_len, uint8_t file_type) {
    struct EXT2DirectoryEntry entry;

    memset(&entry, 0, sizeof(entry));
    entry.inode = inode;
    entry.rec_len = rec_len;
    entry.name_len = name_len;
    entry.file_type = file_type;
    memcpy(destination, &entry, sizeof(entry));
    memcpy((uint8_t *)destination + sizeof(entry), name, name_len);
}

/**
 * @brief Append a child entry to a directory, growing it when needed.
 * @return 0 on success, -1 on failure (no directory change is left behind)
 */
static int8_t ext2_add_directory_entry(uint32_t parent_inode_number,
                                       uint32_t child_inode,
                                       const char *name, uint8_t name_len,
                                       uint8_t file_type) {
    struct EXT2ReadContext context;
    struct EXT2Inode parent;
    struct BlockBuffer block;
    uint16_t new_record_length = get_entry_record_len(name_len);
    uint32_t parent_blocks;

    if (!ext2_read_cached_inode(parent_inode_number, &parent) ||
        (parent.i_mode & 0xF000u) != EXT2_S_IFDIR ||
        parent.i_size == 0 || parent.i_size % BLOCK_SIZE != 0 ||
        parent.i_size > DISK_SPACE) {
        return -1;
    }

    context.superblock = superblock;
    context.bgd_table = bgd_table;
    parent_blocks = parent.i_size / BLOCK_SIZE;

    for (uint32_t logical_block = 0;
         logical_block < parent_blocks;
         logical_block++) {
        uint32_t disk_block;
        uint32_t offset = 0;

        if (!ext2_get_data_block(&context, &parent, logical_block,
                                 &disk_block)) {
            return -1;
        }
        read_blocks(&block, disk_block, 1);

        while (offset + sizeof(struct EXT2DirectoryEntry) <= BLOCK_SIZE) {
            struct EXT2DirectoryEntry entry;
            uint16_t actual_length;

            memcpy(&entry, block.buf + offset, sizeof(entry));
            if (entry.rec_len < sizeof(struct EXT2DirectoryEntry) ||
                (entry.rec_len & 3u) != 0 ||
                entry.rec_len > BLOCK_SIZE - offset) {
                return -1;
            }

            if (entry.inode == 0) {
                if (entry.rec_len >= new_record_length) {
                    ext2_fill_directory_entry(block.buf + offset,
                                              entry.rec_len, child_inode,
                                              name, name_len, file_type);
                    write_blocks(&block, disk_block, 1);
                    return 0;
                }
                offset += entry.rec_len;
                continue;
            }

            if (entry.name_len > UINT8_MAX) {
                return -1;
            }
            actual_length = get_entry_record_len((uint8_t)entry.name_len);
            if (actual_length > entry.rec_len) {
                return -1;
            }
            if (entry.rec_len - actual_length >= new_record_length) {
                uint16_t old_record_length = entry.rec_len;

                entry.rec_len = actual_length;
                memcpy(block.buf + offset, &entry, sizeof(entry));
                /* The new record owns the remainder of the old record. */
                ext2_fill_directory_entry(
                    block.buf + offset + actual_length,
                    (uint16_t)(old_record_length - actual_length),
                    child_inode, name, name_len, file_type);
                write_blocks(&block, disk_block, 1);
                return 0;
            }

            offset += entry.rec_len;
        }
    }

    /* No free record in the existing blocks: append one new directory block. */
    {
        uint32_t logical_block = parent_blocks;
        uint32_t block_number;
        uint32_t pointer_blocks = 0;
        uint32_t preferred_bgd = inode_to_bgd(parent_inode_number);

        if (logical_block >= EXT2_MAX_DATA_BLOCKS ||
            !ext2_allocate_physical_block(preferred_bgd, &block_number)) {
            return -1;
        }

        memset(&block, 0, sizeof(block));
        ext2_fill_directory_entry(block.buf, BLOCK_SIZE, child_inode,
                                  name, name_len, file_type);
        write_blocks(&block, block_number, 1);

        if (!ext2_set_data_block_pointer(&parent, logical_block, block_number,
                                         preferred_bgd, &pointer_blocks)) {
            ext2_release_block(block_number);
            ext2_sync_metadata();
            return -1;
        }

        parent.i_size += BLOCK_SIZE;
        parent.i_blocks += 1u + pointer_blocks;
        sync_node(&parent, parent_inode_number);
        ext2_sync_metadata();
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
                             request.name_len, EXT2_FT_REG_FILE, &entry);
    if (result == 1) {
        return 3;
    }
    if (result == 2) {
        return 4;
    }
    if (result == 3) {
        return 1;
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

int8_t write(struct EXT2DriverRequest *request) {
    struct EXT2ReadContext context;
    struct EXT2DirectoryEntry existing;
    struct EXT2Inode node;
    uint32_t inode_number;
    int8_t result;
    bool is_directory;

    if (request == NULL ||
        !ext2_valid_name(request->name, request->name_len)) {
        return -1;
    }

    is_directory = request->is_directory;
    if (!is_directory) {
        if (request->buffer_size > 0 && request->buf == NULL) {
            return -1;
        }
        if (request->buffer_size > EXT2_MAX_DATA_BLOCKS * BLOCK_SIZE) {
            return -1;
        }
    }

 context.superblock = superblock;
    context.bgd_table = bgd_table;

    /*
     * Names must be unique within the parent directory,
     * regardless of whether the entry is a file or directory.
     */
    result = ext2_find_entry(
        &context,
        request->parent_inode,
        request->name,
        request->name_len,
        EXT2_FT_UNKNOWN,
        &existing
    );

    if (result == 0) {
        /* The name is already in use. */
        return 1;
    }

    if (result == 2) {
        /* The parent is not a valid directory. */
        return 2;
    }

    if (result != 1) {
        /* An unexpected lookup error occurred. */
        return -1;
    }

    inode_number = allocate_node();
    if (inode_number == 0) {
        return -1;
    }

    memset(&node, 0, sizeof(node));
    node.i_mode = is_directory ? EXT2_S_IFDIR : EXT2_S_IFREG;
    node.i_size = is_directory ? BLOCK_SIZE : request->buffer_size;

    if (is_directory) {
        uint32_t group = inode_to_bgd(inode_number);
        uint32_t block_number;

        if (group >= GROUPS_COUNT) {
            deallocate_node(inode_number);
            return -1;
        }
        /* Persist the directory mode first so rollback updates used_dirs_count. */
        sync_node(&node, inode_number);
        bgd_table.table[group].bg_used_dirs_count++;
        ext2_sync_metadata();

        if (!ext2_allocate_physical_block(inode_to_bgd(inode_number),
                                          &block_number)) {
            deallocate_node(inode_number);
            return -1;
        }
        node.i_block[0] = block_number;
        node.i_blocks = 1;
        init_directory_table(&node, inode_number, request->parent_inode);
        sync_node(&node, inode_number);
        ext2_sync_metadata();
    } else {
        if (node.i_size > 0 &&
            !ext2_allocate_node_blocks_checked(request->buf, &node,
                                               inode_to_bgd(inode_number))) {
            deallocate_node(inode_number);
            return -1;
        }
        sync_node(&node, inode_number);
        ext2_sync_metadata();
    }

    result = ext2_add_directory_entry(
        request->parent_inode, inode_number, request->name,
        request->name_len,
        is_directory ? EXT2_FT_DIR : EXT2_FT_REG_FILE);
    if (result != 0) {
        deallocate_node(inode_number);
        return -1;
    }

    return 0;
}