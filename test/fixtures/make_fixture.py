import struct
import sys

BLOCK = 512
TOTAL_BLOCKS = 8192
GROUPS = 8
BLOCKS_PER_GROUP = 1024
INODE_SIZE = 70
INODES_PER_BLOCK = BLOCK // INODE_SIZE          # 7
INODETABLE_BLOCKS = 16
INODES_PER_GROUP = INODES_PER_BLOCK * INODETABLE_BLOCKS  # 112
POINTERS = BLOCK // 4                           # 128 block numbers per indirect block
S_IFDIR, S_IFREG = 0x4000, 0x8000
FT_REG, FT_DIR = 1, 2

disk = bytearray(TOTAL_BLOCKS * BLOCK)


def pattern(size):
    return bytes((i * 13 + size) & 0xFF for i in range(size))


def metadata_start(group):
    """First block of a group's bitmaps + inode table."""
    return 3 if group == 0 else group * BLOCKS_PER_GROUP


def inode_table(group):
    return metadata_start(group) + 2


# ---------- allocation bookkeeping ----------

used_blocks = set()
used_inodes = set()
for g in range(GROUPS):
    start = metadata_start(g)
    used_blocks.update(range(start, start + 2 + INODETABLE_BLOCKS))
used_blocks.update({0, 1, 2})


def alloc_block(group=0):
    """First free block inside a group (first-fit)."""
    lo = group * BLOCKS_PER_GROUP
    for b in range(lo, lo + BLOCKS_PER_GROUP):
        if b not in used_blocks:
            used_blocks.add(b)
            return b
    raise RuntimeError("group %d is full" % group)


next_inode = [1]


def alloc_inode(number=None):
    if number is None:
        while next_inode[0] in used_inodes:
            next_inode[0] += 1
        number = next_inode[0]
    used_inodes.add(number)
    return number


def write_inode(number, mode, size, data_blocks, pointer_blocks):
    """Write an inode; data_blocks fill i_block[0..11], then single, then double indirect."""
    i_block = [0] * 15
    direct = data_blocks[:12]
    i_block[:len(direct)] = direct
    rest = data_blocks[12:]
    if rest:
        single = pointer_blocks.pop(0)
        i_block[12] = single
        chunk = rest[:POINTERS]
        struct.pack_into("<%dI" % len(chunk), disk, single * BLOCK, *chunk)
        rest = rest[POINTERS:]
    if rest:
        double = pointer_blocks.pop(0)
        i_block[13] = double
        index = 0
        while rest:
            level1 = pointer_blocks.pop(0)
            struct.pack_into("<I", disk, double * BLOCK + index * 4, level1)
            chunk = rest[:POINTERS]
            struct.pack_into("<%dI" % len(chunk), disk, level1 * BLOCK, *chunk)
            rest = rest[POINTERS:]
            index += 1
    local = number - 1
    group, slot = divmod(local, INODES_PER_GROUP)
    block = inode_table(group) + slot // INODES_PER_BLOCK
    offset = block * BLOCK + (slot % INODES_PER_BLOCK) * INODE_SIZE
    struct.pack_into("<HII15I", disk, offset, mode, size, len(data_blocks), *i_block)


def pointer_blocks_needed(data_count):
    if data_count <= 12:
        return 0
    if data_count <= 12 + POINTERS:
        return 1
    return 2 + -(-(data_count - 12 - POINTERS) // POINTERS)


def add_file(number, size, group=0):
    count = -(-size // BLOCK)
    blocks = [alloc_block(group) for _ in range(count)]
    pointers = [alloc_block(group) for _ in range(pointer_blocks_needed(count))]
    data = pattern(size)
    for k, b in enumerate(blocks):
        piece = data[k * BLOCK:(k + 1) * BLOCK]
        disk[b * BLOCK:b * BLOCK + len(piece)] = piece
    write_inode(number, S_IFREG, size, blocks, pointers)


def entry_len(name):
    return (9 + len(name) + 3) & ~3


def add_dir(number, parent, children):
    """children: list of (name, inode, file_type). Entries never cross a block edge."""
    entries = [(".", number, FT_DIR), ("..", parent, FT_DIR)] + children
    blocks, current = [], []
    used = 0
    for e in entries:
        need = entry_len(e[0])
        if used + need > BLOCK:
            blocks.append(current)
            current, used = [], 0
        current.append(e)
        used += need
    blocks.append(current)
    numbers = [alloc_block() for _ in blocks]
    for b, items in zip(numbers, blocks):
        offset = 0
        for k, (name, ino, ftype) in enumerate(items):
            # the last entry in a block stretches to the end of the block
            rec = BLOCK - offset if k == len(items) - 1 else entry_len(name)
            struct.pack_into("<IHHB", disk, b * BLOCK + offset, ino, rec, len(name), ftype)
            raw = name.encode()
            disk[b * BLOCK + offset + 9:b * BLOCK + offset + 9 + len(raw)] = raw
            offset += rec
    write_inode(number, S_IFDIR, len(numbers) * BLOCK, numbers, [])


# ---------- the tree ----------

root = alloc_inode(1)
sizes = [0, 1, 511, 512, 513, 6144, 6145, 71680, 71681]
root_children = []
for s in sizes:
    ino = alloc_inode()
    add_file(ino, s)
    root_children.append(("s%d" % s, ino, FT_REG))

docs, a, b, deep = alloc_inode(), alloc_inode(), alloc_inode(), alloc_inode()
add_file(deep, 4)
add_dir(b, a, [("deep.txt", deep, FT_REG)])
add_dir(a, docs, [("b", b, FT_DIR)])
add_dir(docs, root, [("a", a, FT_DIR)])
root_children.append(("docs", docs, FT_DIR))

many = alloc_inode()
many_children = []
for k in range(50):
    ino = alloc_inode()
    add_file(ino, 3)
    many_children.append(("e%02d" % k, ino, FT_REG))
add_dir(many, root, many_children)
root_children.append(("many", many, FT_DIR))

last0 = alloc_inode(112)               # last inode of group 0
add_file(last0, 10)
first1 = alloc_inode(113)              # first inode of group 1, data block in group 1
add_file(first1, 12, group=1)
root_children += [("last0", last0, FT_REG), ("first1", first1, FT_REG)]

add_dir(root, root, root_children)

# ---------- boot block, superblock, group descriptors, bitmaps ----------

signature = bytearray(BLOCK)
rows = ["Course          ", "Designed by     ", "Lab Sister ITB  ", "Made with <3    ", "-----------2025\n"]
signature[:80] = "".join(rows).encode()
signature[510:512] = b"Ok"
disk[0:BLOCK] = signature

free_blocks_total, free_inodes_total = 0, 0
for g in range(GROUPS):
    lo = g * BLOCKS_PER_GROUP
    block_bits = bytearray(BLOCK)
    for b in range(lo, lo + BLOCKS_PER_GROUP):
        if b in used_blocks:
            block_bits[(b - lo) // 8] |= 1 << ((b - lo) % 8)
    inode_bits = bytearray(BLOCK)
    for i in range(INODES_PER_GROUP):
        if g * INODES_PER_GROUP + i + 1 in used_inodes:
            inode_bits[i // 8] |= 1 << (i % 8)
    start = metadata_start(g)
    disk[start * BLOCK:(start + 1) * BLOCK] = block_bits
    disk[(start + 1) * BLOCK:(start + 2) * BLOCK] = inode_bits
    free_b = BLOCKS_PER_GROUP - sum(1 for b in range(lo, lo + BLOCKS_PER_GROUP) if b in used_blocks)
    free_i = INODES_PER_GROUP - sum(1 for i in range(INODES_PER_GROUP) if g * INODES_PER_GROUP + i + 1 in used_inodes)
    dirs = 4 + 1 if g == 0 else 0     # root, docs, a, b, many all live in group 0
    struct.pack_into("<3I4H3I", disk, 2 * BLOCK + g * 32, start, start + 1, inode_table(g),
                     free_b, free_i, dirs, 0, 0, 0, 0)
    free_blocks_total += free_b
    free_inodes_total += free_i

struct.pack_into("<10IHBB", disk, BLOCK, GROUPS * INODES_PER_GROUP, TOTAL_BLOCKS, 0, free_blocks_total, free_inodes_total, 0, 2, BLOCKS_PER_GROUP, BLOCKS_PER_GROUP, INODES_PER_GROUP, 0xEF53, 0, 0)

out = sys.argv[1] if len(sys.argv) > 1 else "test/fixtures/boundary.img"
with open(out, "wb") as f:
    f.write(disk)
print("wrote %s: %d inodes used, %d blocks used" % (out, len(used_inodes), len(used_blocks)))