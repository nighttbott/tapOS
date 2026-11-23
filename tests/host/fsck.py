#!/usr/bin/env python3
"""
fsck.py: health check for a disk image

It shares no code with src/filesystem/ext2.c, so a bug in the C code cannot hide
itself here. Checks:
  1. boot signature and superblock magic
  2. every folder starts with '.' and '..', and entries are 4-byte aligned and fill each block
  3. no block is used twice (by two files, or by a file and the metadata area)
  4. the bitmaps agree with what the folder tree actually uses (no leaks, no lost data)
  5. free counters in the superblock and group descriptors agree with the bitmaps

Usage: python3 tests/host/fsck.py <image> ...   exit code 0 = healthy
"""
import struct
import sys

BLOCK = 512
INODE_SIZE = 70
INODES_PER_BLOCK = BLOCK // INODE_SIZE
POINTERS = BLOCK // 4
S_IFDIR, S_IFREG = 0x4000, 0x8000
GROUPS = BLOCK // 32 // 2   # GROUPS_COUNT in ext2.h: 8 group descriptors

def check(path):
    d = open(path, "rb").read()
    errors = []

    def err(text):
        errors.append(text)

    if d[510:512] != b"Ok" or not d.startswith(b"Course"):
        err("block 0: filesystem signature missing")
    (inodes_count, blocks_count, _r, free_blocks, free_inodes, _first, _first_ino,
     blocks_per_group, _frags, inodes_per_group, magic) = struct.unpack_from("<10IH", d, BLOCK)
    if magic != 0xEF53:
        return ["superblock: magic is %#x, expected 0xEF53 (not a tapOS disk)" % magic]
    groups = GROUPS
    if groups * blocks_per_group < blocks_count:
        err("superblock: %d groups x %d blocks per group cover only %d of %d blocks "
            "(GROUPS_COUNT / BLOCKS_PER_GROUP mismatch)"
            % (groups, blocks_per_group, groups * blocks_per_group, blocks_count))
    bgd = [struct.unpack_from("<3I3H", d, 2 * BLOCK + g * 32) for g in range(groups)]

    owner = {}
    for b in (0, 1, 2):
        owner[b] = "boot/superblock/descriptors"
    for g, (bb, ib, it, _fb, _fi, _dirs) in enumerate(bgd):
        meta = [bb, ib] + list(range(it, it + inodes_per_group // INODES_PER_BLOCK))
        for b in meta:
            if b in owner:
                err("block %d: metadata of group %d overlaps %s" % (b, g, owner[b]))
            owner[b] = "metadata of group %d" % g

    def inode(n):
        g, slot = divmod(n - 1, inodes_per_group)
        blk = bgd[g][2] + slot // INODES_PER_BLOCK
        return struct.unpack_from("<HII15I", d, blk * BLOCK + (slot % INODES_PER_BLOCK) * INODE_SIZE)

    def claim(b, who):
        if not 0 < b < blocks_count:
            err("%s: block number %d is outside the disk" % (who, b))
            return False
        if b in owner:
            err("%s: block %d is already used by %s" % (who, b, owner[b]))
            return False
        owner[b] = who
        return True

    def blocks_of(n, who):
        mode, size, _cnt, *ib = inode(n)
        count = -(-size // BLOCK)
        data = list(ib[:min(count, 12)])
        if count > 12 and claim(ib[12], who + " single-indirect"):
            data += struct.unpack_from("<%dI" % min(count - 12, POINTERS), d, ib[12] * BLOCK)
        if count > 12 + POINTERS and claim(ib[13], who + " double-indirect"):
            left = count - 12 - POINTERS
            for k in range(-(-left // POINTERS)):
                lvl1 = struct.unpack_from("<I", d, ib[13] * BLOCK + 4 * k)[0]
                if claim(lvl1, who + " double-indirect level 1"):
                    data += struct.unpack_from("<%dI" % min(left - k * POINTERS, POINTERS), d, lvl1 * BLOCK)
        for b in data:
            claim(b, who)
        return data

    reachable = {}

    def walk(n, parent, path):
        mode, size, *_ = inode(n)
        if mode & 0xF000 != S_IFDIR:
            err("%s: inode %d is not a folder" % (path, n))
            return
        names = []
        for k, b in enumerate(blocks_of(n, path)):
            off = 0
            while off < BLOCK:
                ino, rec, nlen, ftype = struct.unpack_from("<IHHB", d, b * BLOCK + off)
                if rec < 12 or rec % 4 or off + rec > BLOCK:
                    err("%s block %d: bad rec_len %d at offset %d" % (path, b, rec, off))
                    break
                name = d[b * BLOCK + off + 9:b * BLOCK + off + 9 + nlen].decode(errors="replace")
                if ino:
                    names.append((name, ino, ftype))
                off += rec
        if len(names) < 2 or names[0][:2] != (".", n) or names[1][:2] != ("..", parent):
            err("%s: must start with '.'=%d and '..'=%d, found %s" % (path, n, parent, names[:2]))
        for name, ino, ftype in names[2:]:
            child = path.rstrip("/") + "/" + name
            if ino in reachable:
                err("%s: inode %d is also %s" % (child, ino, reachable[ino]))
                continue
            reachable[ino] = child
            cmode = inode(ino)[0] & 0xF000
            if ftype == 2:
                if cmode != S_IFDIR:
                    err("%s: entry says folder but inode mode is %#x" % (child, cmode))
                walk(ino, n, child)
            else:
                if cmode != S_IFREG:
                    err("%s: entry says file but inode mode is %#x" % (child, cmode))
                blocks_of(ino, child)

    reachable[1] = "/"
    walk(1, 1, "/")

    total_free_b, total_free_i = 0, 0
    for g, (bb, ib, _it, fb, fi, _dirs) in enumerate(bgd):
        lo = g * blocks_per_group
        bits = d[bb * BLOCK:(bb + 1) * BLOCK]
        zeros = 0
        for b in range(lo, min(lo + blocks_per_group, blocks_count)):
            used = bits[(b - lo) // 8] >> ((b - lo) % 8) & 1
            zeros += not used
            if b in owner and not used:
                err("block %d is in use (%s) but free in the bitmap" % (b, owner[b]))
            if used and b not in owner:
                err("block %d is marked used but nothing uses it (leak)" % b)
        if zeros != fb:
            err("group %d: descriptor says %d free blocks, bitmap says %d" % (g, fb, zeros))
        total_free_b += zeros
        ibits = d[ib * BLOCK:(ib + 1) * BLOCK]
        izeros = 0
        for k in range(inodes_per_group):
            n = g * inodes_per_group + k + 1
            if n > inodes_count:
                break
            used = ibits[k // 8] >> (k % 8) & 1
            izeros += not used
            if n in reachable and not used:
                err("inode %d (%s) is in use but free in the bitmap" % (n, reachable[n]))
            if used and n not in reachable:
                err("inode %d is marked used but no folder points to it (orphan)" % n)
        if izeros != fi:
            err("group %d: descriptor says %d free inodes, bitmap says %d" % (g, fi, izeros))
        total_free_i += izeros
    if free_blocks != total_free_b:
        err("superblock says %d free blocks, bitmaps say %d" % (free_blocks, total_free_b))
    if free_inodes != total_free_i:
        err("superblock says %d free inodes, bitmaps say %d" % (free_inodes, total_free_i))
    return errors

def main():
    bad = 0
    for path in sys.argv[1:]:
        errors = check(path)
        if errors:
            bad += 1
            print("FSCK FAIL %s: %d problem(s)" % (path, len(errors)))
            for e in errors[:20]:
                print("    " + e)
            if len(errors) > 20:
                print("    ... %d more" % (len(errors) - 20))
        else:
            print("FSCK OK   %s" % path)
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
