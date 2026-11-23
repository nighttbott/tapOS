#ifndef _FS_INDEX_H
#define _FS_INDEX_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Name index of the whole file system, used by `find` so it does not have to walk every folder.
 *
 * One node per inode records its parent folder, name and type. A hash table over the names
 * (FNV-1a, chained) finds every entry with a given name in a few steps, and a match's full path
 * is rebuilt by following the parent links. The index is built by one walk from the root the
 * first time it is needed, then kept up to date by write() and delete() entry by entry.
 * Anything it cannot patch cheaply (move) or that replaces the disk (format) throws it away,
 * and the next lookup rebuilds it, so it can never disagree with the disk.
 */

#define FS_INDEX_NO_MATCH_ERROR   (-1)   /* the file system could not be read */
#define FS_INDEX_TRUNCATED        (-2)   /* the output buffer filled up; what fits was written */

/** Forgets the index; the next fs_index_find() rebuilds it from the disk. */
void fs_index_invalidate(void);

/** Records a new directory entry. Does nothing while the index is not built. */
void fs_index_add(uint32_t parent_inode, uint32_t inode, const char *name, uint8_t name_len,
                  uint8_t file_type);

/** Forgets the entry of a deleted inode. Does nothing while the index is not built. */
void fs_index_remove(uint32_t inode);

/**
 * Writes the absolute path of every file and folder called `name`, one per line and with a
 * trailing '/' for folders, to `out` (not NUL-terminated).
 *
 * @param written_out bytes written to out
 * @return number of matches (0 or more), FS_INDEX_TRUNCATED, or FS_INDEX_NO_MATCH_ERROR
 */
int32_t fs_index_find(const char *name, uint8_t name_len, char *out, uint32_t out_size,
                      uint32_t *written_out);

/** Number of entries in the index (builds it if needed); -1 when the disk cannot be read. */
int32_t fs_index_count(void);

#endif
