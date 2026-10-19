#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/driver/disk.h"
#include "header/filesystem/ext2.h"
#include "fs_test.h"
#include "fs_cases.h"

// defined in src/filesystem/ext2.c
extern const uint8_t fs_signature[BLOCK_SIZE];

#define ROOT_INODE 1u
#define BIG_BUFFER_SIZE (72u * 1024u)
#define DIR_BUFFER_SIZE (8u * BLOCK_SIZE)

static uint8_t big[BIG_BUFFER_SIZE];
static uint8_t dir_buf[DIR_BUFFER_SIZE];
static struct BlockBuffer block;

/* HELPERS */

static uint8_t text_len(const char *text) {
    uint8_t n = 0;
    while (text[n] != '\0')
        n++;
    return n;
}

static bool same_bytes(const uint8_t *a, const uint8_t *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

// build name like 'w6145'
static void name_with_number(char *out, const char *prefix, uint32_t number) {
    char digits[11];
    uint32_t n = 0, pos = 0;
 
    while (prefix[pos] != '\0') {
        out[pos] = prefix[pos];
        pos++;
    }
    do {
        digits[n++] = (char) ('0' + number % 10u);
        number /= 10u;
    } while (number > 0);
    while (n > 0)
        out[pos++] = digits[--n];
    out[pos] = '\0';
}

static void fill_pattern(uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        big[i] = fs_test_pattern(i, size);
    }
}

static struct EXT2DriverRequest make_req(void *buf, const char *name, uint32_t parent, uint32_t size, bool is_directory) {
    struct EXT2DriverRequest r;
    r.buf = buf;
    r.name = (char *) name;
    r.name_len = text_len(name);
    r.parent_inode = parent;
    r.buffer_size = size;
    r.is_directory = is_directory;
    return r;
}

/*
 * find the inode number of "name" inside folder "parent" by rading the folder
 * with C's read_directory(".") and walking its entries. ret 0 if not found 
*/
static uint32_t find_inode(uint32_t parent, const char *name) {
    struct EXT2DriverRequest r = make_req(dir_buf, ".", parent, DIR_BUFFER_SIZE, true);
    uint8_t len = text_len(name);
 
    if (read_directory(&r) != 0)
        return 0;
    for (uint32_t blk = 0; blk < DIR_BUFFER_SIZE / BLOCK_SIZE; blk++) {
        uint32_t offset = 0;
        while (offset + sizeof(struct EXT2DirectoryEntry) <= BLOCK_SIZE) {
            struct EXT2DirectoryEntry *e =
                (struct EXT2DirectoryEntry *) (dir_buf + blk * BLOCK_SIZE + offset);
            const uint8_t *entry_name = (const uint8_t *) e + sizeof(struct EXT2DirectoryEntry);
            if (e->rec_len == 0)
                break;
            if (e->inode != 0 && e->name_len == len &&
                same_bytes(entry_name, (const uint8_t *) name, len))
                return e->inode;
            offset += e->rec_len;
        }
    }
    return 0;
}

// count used entries (inode != 0) in the folder buf last filled by read_directory
static uint32_t count_entries(uint32_t blocks) {
    uint32_t count = 0;
    for (uint32_t blk = 0; blk < blocks; blk++) {
        uint32_t offset = 0;
        while (offset + sizeof(struct EXT2DirectoryEntry) <= BLOCK_SIZE) {
            struct EXT2DirectoryEntry *e =
                (struct EXT2DirectoryEntry *) (dir_buf + blk * BLOCK_SIZE + offset);
            if (e->rec_len == 0)
                break;
            if (e->inode != 0)
                count++;
            offset += e->rec_len;
        }
    }
    return count;
}

// free block and inode counters from superblock, used to detect leaked space
struct FreeCounts {
    uint32_t blocks;
    uint32_t inodes;
};

static struct FreeCounts read_free_counts(void) {
    struct FreeCounts c;
    struct EXT2Superblock *sb = (struct EXT2Superblock *) block.buf;
    read_blocks(&block, 1, 1);
    c.blocks = sb->s_free_blocks_count;
    c.inodes = sb->s_free_inodes_count;
    return c;
}

// passes when ok is true and both free counters are unchanged; on failure prints the counter that moved
static void check_counts(struct FsTestContext *ctx, const char *id, const char *what, bool ok,struct FreeCounts before, struct FreeCounts after) {
    bool block_same = before.blocks == after.blocks;
    bool inode_same = before.inodes == after.inodes;
    if (!block_same || inode_same)
        fs_test_check(ctx, id, what, ok && block_same && inode_same,
                      (int32_t) before.blocks, (int32_t) after.blocks);
    else
        fs_test_check(ctx, id, "(free inodes) leaked", false,
                      (int32_t) before.inodes, (int32_t) after.inodes);
}

// FNV-1a hash over the whole 4 MiB disk, to prove that nothing was written
static uint32_t disk_checksum(void) {
    uint32_t hash = 2166136261u;
    for (uint32_t lba = 0; lba < DISK_SPACE / BLOCK_SIZE; lba++) {
        read_blocks(&block, lba, 1);
        for (uint32_t i = 0; i < BLOCK_SIZE; i++)
            hash = (hash ^ block.buf[i]) * 16777619u;
    }
    return hash;
}

// Calls initialize_filesystem_ext2(); returns false (and records TODO) if it is still a placeholder
static bool try_initialize(struct FsTestContext *ctx, const char *id, const char *what) {
    uint32_t hits = fs_stub_hits;
    initialize_filesystem_ext2();
    if (fs_stub_hits != hits) {
        fs_test_not_implemented(ctx, id, what, "initialize_filesystem_ext2");
        return false;
    }
    return true;
}
 
// Records a return-code check, or TODO when the function is still a placeholder
static void check_rc(struct FsTestContext *ctx, const char *id, const char *what,
                     int8_t rc, int8_t expected, const char *function_name) {
    if (rc == FS_TEST_NOT_IMPLEMENTED)
        fs_test_not_implemented(ctx, id, what, function_name);
    else
        fs_test_check(ctx, id, what, rc == expected, expected, rc);
}
 
// Reads a file into big and checks both the return code and every byte
static void check_read_pattern(struct FsTestContext *ctx, const char *id, const char *what,
                               const char *name, uint32_t parent, uint32_t size) {
    struct EXT2DriverRequest r = make_req(big, name, parent, BIG_BUFFER_SIZE, false);
    int8_t rc = read(r);
    int32_t bad = (rc == 0) ? fs_test_first_mismatch(big, size) : -2;
    // On failure "actual" is the read code if read failed, otherwise the first wrong byte offset
    fs_test_check(ctx, id, what, rc == 0 && bad == -1, rc == 0 ? -1 : 0, rc == 0 ? bad : rc);
}
 
/* ---------- suite: team fixture (C) ---------- */
 
void fs_suite_team_fixture(struct FsTestContext *ctx) {
    static const uint8_t laur[] = "Grievous Lady";
    static const uint8_t sasakure[] = "TONDEMO-WONDERZ";
    static const uint8_t tpazolite[] = "Oshama Scramble!";
    struct EXT2DriverRequest r;
    struct EXT2DirectoryEntry *first = (struct EXT2DirectoryEntry *) big;
    int8_t rc;
 
    fs_test_info(ctx, "read on test/fixtures/sample-image.bin (owner C)");
 
    r = make_req(big, "laur", ROOT_INODE, BIG_BUFFER_SIZE, false);
    rc = read(r);
    fs_test_check(ctx, "R01", "read /laur", rc == 0 && same_bytes(big, laur, 13), 0, rc);
 
    r = make_req(big, "sasakure", ROOT_INODE, BIG_BUFFER_SIZE, false);
    rc = read(r);
    fs_test_check(ctx, "R02", "read /sasakure (inode 8, 2nd inode-table block)",
                  rc == 0 && same_bytes(big, sasakure, 15), 0, rc);
 
    r = make_req(big, "tpazolite", 2, BIG_BUFFER_SIZE, false);
    rc = read(r);
    fs_test_check(ctx, "R03", "read /folder1/tpazolite",
                  rc == 0 && same_bytes(big, tpazolite, 16), 0, rc);
 
    r = make_req(big, "folder1", ROOT_INODE, BIG_BUFFER_SIZE, false);
    rc = read(r);
    fs_test_check(ctx, "R04", "read a folder as a file -> 1", rc == 1, 1, rc);
 
    r = make_req(big, "laur", ROOT_INODE, 5, false);
    rc = read(r);
    fs_test_check(ctx, "R05", "buffer too small -> 2", rc == 2, 2, rc);
 
    r = make_req(big, "nope", ROOT_INODE, BIG_BUFFER_SIZE, false);
    rc = read(r);
    fs_test_check(ctx, "R06", "name not found -> 3", rc == 3, 3, rc);
 
    r = make_req(big, "laur", 7, BIG_BUFFER_SIZE, false);
    rc = read(r);
    fs_test_check(ctx, "R07", "parent is a file -> 4", rc == 4, 4, rc);
 
    r = make_req(big, "laur", 999, BIG_BUFFER_SIZE, false);
    rc = read(r);
    fs_test_check(ctx, "R08", "parent inode out of range -> 4", rc == 4, 4, rc);
 
    r = make_req(big, "folder1", ROOT_INODE, BIG_BUFFER_SIZE, true);
    rc = read_directory(&r);
    fs_test_check(ctx, "D01", "read_directory /folder1, first entry is '.' = inode 2",
                  rc == 0 && first->inode == 2 && first->name_len == 1, 0, rc);
 
    r = make_req(big, "laur", ROOT_INODE, BIG_BUFFER_SIZE, true);
    rc = read_directory(&r);
    fs_test_check(ctx, "D02", "read_directory on a file -> 1", rc == 1, 1, rc);
 
    r = make_req(big, "nope", ROOT_INODE, BIG_BUFFER_SIZE, true);
    rc = read_directory(&r);
    fs_test_check(ctx, "D03", "read_directory name not found -> 2", rc == 2, 2, rc);
 
    r = make_req(big, "folder1", 7, BIG_BUFFER_SIZE, true);
    rc = read_directory(&r);
    fs_test_check(ctx, "D04", "read_directory with a file as parent -> 3", rc == 3, 3, rc);
 
    r = make_req(big, "..", 3, BIG_BUFFER_SIZE, true);
    rc = read_directory(&r);
    fs_test_check(ctx, "D05", "read_directory '..' from nestedf1 gives folder1 (inode 2)",
                  rc == 0 && first->inode == 2, 0, rc);
 
    r = make_req(big, "folder1", ROOT_INODE, 100, true);
    rc = read_directory(&r);
    fs_test_check(ctx, "D06", "read_directory with a 100-byte buffer is rejected", rc != 0, -1, rc);
}
 
/* ---------- suite: boundary fixture (C) ---------- */
 
static const uint32_t boundary_sizes[] = { 0, 1, 511, 512, 513, 6144, 6145, 71680, 71681 };
static const char *const boundary_ids[] = { "B01", "B02", "B03", "B04", "B05", "B06", "B07", "B08", "B09" };
static const char *const boundary_what[] = {
    "read s0 (empty file)", "read s1", "read s511 (just under one block)",
    "read s512 (exactly one block)", "read s513 (one byte into block 2)",
    "read s6144 (last direct block)", "read s6145 (first single-indirect block)",
    "read s71680 (last single-indirect block)", "read s71681 (first double-indirect block)",
};
 
void fs_suite_boundary(struct FsTestContext *ctx) {
    char name[16];
    struct EXT2DriverRequest r;
    struct EXT2DirectoryEntry *first = (struct EXT2DirectoryEntry *) dir_buf;
    uint32_t docs, a, b;
    int8_t rc;
 
    fs_test_info(ctx, "read on test/fixtures/boundary.img (owner C)");
 
    for (uint32_t i = 0; i < sizeof(boundary_sizes) / sizeof(boundary_sizes[0]); i++) {
        name_with_number(name, "s", boundary_sizes[i]);
        check_read_pattern(ctx, boundary_ids[i], boundary_what[i], name, ROOT_INODE, boundary_sizes[i]);
    }
 
    r = make_req(big, "s513", ROOT_INODE, 513, false);
    rc = read(r);
    fs_test_check(ctx, "B10", "read s513 into a buffer of exactly 513 bytes", rc == 0, 0, rc);
 
    r = make_req(big, "s513", ROOT_INODE, 512, false);
    rc = read(r);
    fs_test_check(ctx, "B11", "read s513 into a 512-byte buffer -> 2", rc == 2, 2, rc);
 
    docs = find_inode(ROOT_INODE, "docs");
    a = docs ? find_inode(docs, "a") : 0;
    b = a ? find_inode(a, "b") : 0;
    fs_test_check(ctx, "N01", "walk /docs/a/b by name", b != 0, 1, (int32_t) b);
    check_read_pattern(ctx, "N02", "read /docs/a/b/deep.txt", "deep.txt", b, 4);
 
    r = make_req(dir_buf, "b", a, DIR_BUFFER_SIZE, true);
    rc = read_directory(&r);
    fs_test_check(ctx, "N03", "read_directory /docs/a/b, '.' is b itself",
                  rc == 0 && first->inode == b, 0, rc);
 
    r = make_req(dir_buf, "many", ROOT_INODE, DIR_BUFFER_SIZE, true);
    rc = read_directory(&r);
    fs_test_check(ctx, "L01", "read_directory /many (folder spans 2 blocks) -> 52 entries",
                  rc == 0 && count_entries(2) == 52, 52, rc == 0 ? (int32_t) count_entries(2) : rc);
    check_read_pattern(ctx, "L02", "read /many/e49 (entry in the folder's 2nd block)",
                       "e49", find_inode(ROOT_INODE, "many"), 3);
 
    check_read_pattern(ctx, "G01", "read /last0 (inode 112, last of group 0)", "last0", ROOT_INODE, 10);
    check_read_pattern(ctx, "G02", "read /first1 (inode 113, first of group 1)", "first1", ROOT_INODE, 12);
}
 
/* ---------- suite: blank disk, the full life cycle (B and E) ---------- */
 
static const uint32_t write_sizes[] = { 1, 511, 512, 513, 6144, 6145, 71680, 71681 };
 
static const char *const blank_ids[] = {
    "W01", "W02", "W03", "W04", "W05", "X01", "X02", "X03", "X04", "X05", "P01", "W06",
};
 
void fs_suite_blank(struct FsTestContext *ctx) {
    char name[16];
    struct EXT2DriverRequest r;
    struct EXT2DirectoryEntry *first = (struct EXT2DirectoryEntry *) dir_buf;
    struct EXT2Superblock *sb = (struct EXT2Superblock *) block.buf;
    struct FreeCounts before, after;
    uint32_t hello, dir1, dir2, sum;
    int8_t rc;
 
    fs_test_info(ctx, "format a zeroed disk, then create / read / delete (owners B and E)");
 
    if (!try_initialize(ctx, "I01", "format a zeroed disk")) {
        for (uint32_t i = 0; i < sizeof(blank_ids) / sizeof(blank_ids[0]); i++)
            fs_test_not_implemented(ctx, blank_ids[i], "blocked by I01", "initialize_filesystem_ext2");
        return;
    }
 
    read_blocks(&block, 0, 1);
    fs_test_check(ctx, "I01a", "block 0 holds fs_signature",
                  same_bytes(block.buf, fs_signature, BLOCK_SIZE), 1, 0);
    read_blocks(&block, 1, 1);
    fs_test_check(ctx, "I01b", "superblock magic is 0xEF53",
                  sb->s_magic == EXT2_SUPER_MAGIC, EXT2_SUPER_MAGIC, sb->s_magic);
    r = make_req(dir_buf, ".", ROOT_INODE, DIR_BUFFER_SIZE, true);
    rc = read_directory(&r);
    fs_test_check(ctx, "I01c", "root starts with '.' and '..', both inode 1",
                  rc == 0 && first->inode == ROOT_INODE &&
                  ((struct EXT2DirectoryEntry *) (dir_buf + first->rec_len))->inode == ROOT_INODE,
                  0, rc);
 
    fill_pattern(513);
    r = make_req(big, "hello.txt", ROOT_INODE, 513, false);
    rc = write(&r);
    check_rc(ctx, "W01a", "write /hello.txt (513 bytes)", rc, 0, "write");
    if (rc == 0)
        check_read_pattern(ctx, "W01b", "read /hello.txt back, every byte equal", "hello.txt", ROOT_INODE, 513);
 
    rc = write(&r);
    check_rc(ctx, "W02", "write /hello.txt again -> 1 already exists", rc, 1, "write");
 
    r = make_req(big, "x.txt", 999, 513, false);
    rc = write(&r);
    check_rc(ctx, "W03a", "write with parent inode 999 -> 2", rc, 2, "write");
    hello = find_inode(ROOT_INODE, "hello.txt");
    r = make_req(big, "x.txt", hello ? hello : 999, 513, false);
    rc = write(&r);
    check_rc(ctx, "W03b", "write with a file as parent -> 2", rc, 2, "write");
 
    r = make_req(NULL, "dir1", ROOT_INODE, 0, true);
    rc = write(&r);
    check_rc(ctx, "W04a", "create folder /dir1", rc, 0, "write");
    if (rc == 0) {
        dir1 = find_inode(ROOT_INODE, "dir1");
        r = make_req(dir_buf, "dir1", ROOT_INODE, DIR_BUFFER_SIZE, true);
        rc = read_directory(&r);
        fs_test_check(ctx, "W04b", "/dir1 has '.' = itself and '..' = root",
                      rc == 0 && first->inode == dir1 &&
                      ((struct EXT2DirectoryEntry *) (dir_buf + first->rec_len))->inode == ROOT_INODE,
                      0, rc);
    }
 
    for (uint32_t i = 0; i < sizeof(write_sizes) / sizeof(write_sizes[0]); i++) {
        name_with_number(name, "w", write_sizes[i]);
        fill_pattern(write_sizes[i]);
        r = make_req(big, name, ROOT_INODE, write_sizes[i], false);
        rc = write(&r);
        check_rc(ctx, "W05a", name, rc, 0, "write");
        if (rc == 0)
            check_read_pattern(ctx, "W05b", name, name, ROOT_INODE, write_sizes[i]);
    }
 
    r = make_req(NULL, "hello.txt", ROOT_INODE, 0, false);
    rc = delete(r);
    check_rc(ctx, "X01a", "delete /hello.txt", rc, 0, "delete");
    if (rc == 0) {
        r = make_req(big, "hello.txt", ROOT_INODE, BIG_BUFFER_SIZE, false);
        rc = read(r);
        fs_test_check(ctx, "X01b", "read deleted file -> 3 not found", rc == 3, 3, rc);
    }
 
    r = make_req(NULL, "dir2", ROOT_INODE, 0, true);
    write(&r);
    dir2 = find_inode(ROOT_INODE, "dir2");
    fill_pattern(10);
    r = make_req(big, "inner", dir2 ? dir2 : 999, 10, false);
    write(&r);
    r = make_req(NULL, "dir2", ROOT_INODE, 0, true);
    rc = delete(r);
    check_rc(ctx, "X02", "delete a folder that is not empty -> 2", rc, 2, "delete");
 
    sum = disk_checksum();
    r = make_req(NULL, ".", ROOT_INODE, 0, true);
    rc = delete(r);
    r = make_req(NULL, "..", ROOT_INODE, 0, true);
    int8_t rc2 = delete(r);
    if (rc == FS_TEST_NOT_IMPLEMENTED)
        fs_test_not_implemented(ctx, "X03", "delete '.' and '..' is refused", "delete");
    else
        fs_test_check(ctx, "X03", "delete '.' and '..' is refused, disk unchanged",
                      rc != 0 && rc2 != 0 && disk_checksum() == sum, 1, rc);
 
    r = make_req(NULL, "ghost", ROOT_INODE, 0, false);
    rc = delete(r);
    check_rc(ctx, "X04", "delete a missing name -> 1", rc, 1, "delete");
 
    before = read_free_counts();
    fill_pattern(6145);
    r = make_req(big, "tmp", ROOT_INODE, 6145, false);
    rc = write(&r);
    r = make_req(NULL, "tmp", ROOT_INODE, 0, false);
    rc2 = delete(r);
    after = read_free_counts();
    if (rc == FS_TEST_NOT_IMPLEMENTED || rc2 == FS_TEST_NOT_IMPLEMENTED)
        fs_test_not_implemented(ctx, "X05", "create, delete: all space comes back", "write/delete");
    else
        check_counts(ctx, "X05", "create then delete: free blocks and inodes back to the start value",
                     rc == 0 && rc2 == 0, before, after);
 
    if (try_initialize(ctx, "P01", "restart (initialize again), data still there"))
        check_read_pattern(ctx, "P01", "restart, then read /w71681", "w71681", ROOT_INODE, 71681);
 
#ifdef FS_TEST_SKIP_FULL_DISK
    fs_test_info(ctx, "W06 skipped here (full disk is tested by test/host/run_host_tests.sh)");
    return;
#endif
    fill_pattern(65536);
    rc = 0;
    uint32_t written = 0;
    for (uint32_t i = 0; i < 100 && rc == 0; i++) {
        name_with_number(name, "full", i);
        before = read_free_counts();
        r = make_req(big, name, ROOT_INODE, 65536, false);
        rc = write(&r);
        if (rc == 0)
            written++;
    }
    after = read_free_counts();
    if (rc == FS_TEST_NOT_IMPLEMENTED) {
        fs_test_not_implemented(ctx, "W06", "full disk fails cleanly", "write");
        return;
    }
    check_counts(ctx, "W06a", "full disk: the failing write leaks no space", rc != 0, before, after);
 
    // every file that write() accepted must really be there
    uint32_t readable = 0;
    for (uint32_t i = 0; i < written; i++) {
        name_with_number(name, "full", i);
        r = make_req(big, name, ROOT_INODE, BIG_BUFFER_SIZE, false);
        if (read(r) == 0 && fs_test_first_mismatch(big, 65536) == -1)
            readable++;
    }
    fs_test_check(ctx, "W06b", "every file written before the disk filled reads back",
                  written > 0 && readable == written, (int32_t) written, (int32_t) readable);
}
 
/* ---------- suite: initialize on an existing disk ---------- */
 
void fs_suite_existing(struct FsTestContext *ctx) {
    uint32_t sum;
 
    fs_test_info(ctx, "initialize on a valid disk must not reformat it (owner B)");
    sum = disk_checksum();
    if (try_initialize(ctx, "I02", "initialize on an existing disk"))
        fs_test_check(ctx, "I02", "initialize on an existing disk changes no byte",
                      disk_checksum() == sum, 1, 0);
}
 
/* ---------- suites: two-boot persistence (QEMU) ---------- */
 
void fs_suite_persist_write(struct FsTestContext *ctx) {
    struct EXT2DriverRequest r;
    int8_t rc;
 
    fs_test_info(ctx, "boot 1 of 2: write data that must survive a restart");
    if (!try_initialize(ctx, "S01", "format or load the disk"))
        return;
    fill_pattern(71681);
    r = make_req(big, "persist.bin", ROOT_INODE, 71681, false);
    rc = write(&r);
    check_rc(ctx, "S01", "write /persist.bin (71,681 bytes)", rc, 0, "write");
    r = make_req(NULL, "pdir", ROOT_INODE, 0, true);
    rc = write(&r);
    check_rc(ctx, "S02", "create folder /pdir", rc, 0, "write");
}
 
void fs_suite_persist_read(struct FsTestContext *ctx) {
    struct EXT2DriverRequest r;
    int8_t rc;
 
    fs_test_info(ctx, "boot 2 of 2: data from boot 1 is still there");
    if (!try_initialize(ctx, "S03", "load the disk"))
        return;
    check_read_pattern(ctx, "S03", "read /persist.bin after restart", "persist.bin", ROOT_INODE, 71681);
    r = make_req(dir_buf, "pdir", ROOT_INODE, DIR_BUFFER_SIZE, true);
    rc = read_directory(&r);
    fs_test_check(ctx, "S04", "read_directory /pdir after restart", rc == 0, 0, rc);
}
 
/* ---------- suites: raw disk driver (A) ---------- */
 
static uint8_t raw[255u * BLOCK_SIZE];
 
struct RawCase {
    const char *id;
    const char *what;
    uint32_t lba;
    uint8_t count;
};
 
static const struct RawCase raw_cases[] = {
    { "K01", "1 block at LBA 8191 (last block)", 8191, 1 },
    { "K02", "2 blocks at LBA 8100", 8100, 2 },
    { "K03", "255 blocks at LBA 7800", 7800, 255 },
};
 
static uint8_t raw_pattern(uint32_t lba, uint32_t i) {
    return (uint8_t) ((lba * 31u + i * 7u + 1u) & 0xFFu);
}
 
void fs_suite_disk_write(struct FsTestContext *ctx) {
    fs_test_info(ctx, "boot 1 of 2: write and read back raw blocks (owner A)");
    for (uint32_t c = 0; c < sizeof(raw_cases) / sizeof(raw_cases[0]); c++) {
        uint32_t bytes = (uint32_t) raw_cases[c].count * BLOCK_SIZE;
        for (uint32_t i = 0; i < bytes; i++)
            raw[i] = raw_pattern(raw_cases[c].lba, i);
        write_blocks(raw, raw_cases[c].lba, raw_cases[c].count);
        for (uint32_t i = 0; i < bytes; i++)
            raw[i] = 0;
        read_blocks(raw, raw_cases[c].lba, raw_cases[c].count);
        int32_t bad = -1;
        for (uint32_t i = 0; i < bytes && bad < 0; i++)
            if (raw[i] != raw_pattern(raw_cases[c].lba, i))
                bad = (int32_t) i;
        fs_test_check(ctx, raw_cases[c].id, raw_cases[c].what, bad == -1, -1, bad);
    }
}
 
void fs_suite_disk_read(struct FsTestContext *ctx) {
    fs_test_info(ctx, "boot 2 of 2: raw blocks from boot 1 are still there (owner A)");
    for (uint32_t c = 0; c < sizeof(raw_cases) / sizeof(raw_cases[0]); c++) {
        uint32_t bytes = (uint32_t) raw_cases[c].count * BLOCK_SIZE;
        read_blocks(raw, raw_cases[c].lba, raw_cases[c].count);
        int32_t bad = -1;
        for (uint32_t i = 0; i < bytes && bad < 0; i++)
            if (raw[i] != raw_pattern(raw_cases[c].lba, i))
                bad = (int32_t) i;
        fs_test_check(ctx, raw_cases[c].id, raw_cases[c].what, bad == -1, -1, bad);
    }
}
