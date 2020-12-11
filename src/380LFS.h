#ifndef _380LFS_H_
#define _380LFS_H_

#define FUSE_USE_VERSION 26

#include <stdint.h>
#include <sys/stat.h>

#define PRIVATE_DATA ((struct lfs_data*) fuse_get_context()->private_data)

#define GB (1 << 30)
#define BLOCK_SIZE (1 << 12)
#define OFFSETS_PER_BLOCK (BLOCK_SIZE / 8)
#define DIRECT_BLOCK_COUNT 10
#define INDIRECT_BLOCK_COUNT (OFFSETS_PER_BLOCK * OFFSETS_PER_BLOCK)
#define MIN_INDIRECT_OFFSET (DIRECT_BLOCK_COUNT * BLOCK_SIZE)
#define MAX_BLOCK_COUNT (DIRECT_BLOCK_COUNT + INDIRECT_BLOCK_COUNT)
#define MAX_FILE_SIZE (BLOCK_SIZE * MAX_BLOCK_COUNT)
#define MAX_FILENAME 252
#define MAX_INUMBER ((OFFSETS_PER_BLOCK - 1) * (OFFSETS_PER_BLOCK - 1))

#define SEGMENT_SIZE (1 << 20)
#define BLOCKS_PER_SEGMENT (SEGMENT_SIZE / BLOCK_SIZE)

// round up/down to the nearest multiple of BLOCK_SIZE
#define ROUND_DOWN_BLOCK(size) ((size) / BLOCK_SIZE * BLOCK_SIZE)
#define ROUND_UP_BLOCK(size) ROUND_DOWN_BLOCK((size) + BLOCK_SIZE)

#define ROOT_INUMBER 0
#define INODE_TO_IMAP(inumber) (inumber / (OFFSETS_PER_BLOCK - 1))
#define INODE_TO_IMAP_INDEX(inumber) (inumber % (OFFSETS_PER_BLOCK - 1))

#define INDIRECT_BLOCK(block) (block - DIRECT_BLOCK_COUNT)
#define DOUBLE_INDIRECT_INDEX(block) (INDIRECT_BLOCK(block) / OFFSETS_PER_BLOCK)
#define INDIRECT_INDEX(block) (INDIRECT_BLOCK(block) % OFFSETS_PER_BLOCK)

struct segsum_entry {
    // inumber of file each block belongs to
    // 0 for clean/unused
    // SEGSUM_METADATA for imaps, SEGSUM_ROOT for root (because 0 is taken)
    int file_owner;
    // offset into file of block
    // SEGSUM_METADATA for a file's inode
    off_t file_offset;
};

struct segment_summary {
    int live_bytes;
    struct timespec last_write_time;
    struct segsum_entry entries[BLOCKS_PER_SEGMENT];
};

struct lfs_data {
    char* log_name;
    off_t log_size;
    int fd;
    off_t tail;
    int file_count;
    int max_inumber;
    int segment_count;
    int clean_segments;
    struct segment_summary* segsums;
};

struct superblock {
    int segment_size;
    int block_size;
    off_t inode_map_blocks[OFFSETS_PER_BLOCK - 1];
};

//TODO: keep all inode_maps in memory like real LFS
struct inode_map {
    off_t offset;
    off_t inode_blocks[OFFSETS_PER_BLOCK - 1];
};

struct inode {
    off_t offset;
    struct stat statbuf;
    off_t direct_blocks[DIRECT_BLOCK_COUNT];
    off_t double_indirect_block;
};

struct dir_entry {
    int inumber;
    char name[MAX_FILENAME];
};

struct dir_block {
    struct dir_entry entries[16];
};

struct open_file {
    struct inode file_inode;
    int flags;
};

#endif
