#ifndef _METADATA_HELPERS_H_
#define _METADATA_HELPERS_H_

#include "380LFS.h"
#include "segments.h"

#include <stddef.h>
#include <stdbool.h>

struct superblock* get_superblock(struct superblock*);
int get_inumber(const char*, struct superblock*, struct inode_map*,
                struct inode*);
struct inode* get_inode(int, struct superblock*, struct inode*);
struct inode_map* get_imap(int, struct superblock*, struct inode_map*);
int alloc_inumber(struct superblock*);
int commit_write(off_t, struct superblock*);
int init_data(struct lfs_data*);
int init_fh(struct inode*, uint64_t*);
off_t* read_double_indirect(struct inode*, off_t[OFFSETS_PER_BLOCK]);
off_t* read_indirect(off_t[OFFSETS_PER_BLOCK], int, off_t[OFFSETS_PER_BLOCK]);
off_t get_block_offset(int, struct inode*);
int read_block(int, struct inode*, char[BLOCK_SIZE]);
int read_block_range(int, int, struct inode*, char*);
int read_blocks_all(struct inode*, char*);
int log_append(struct superblock*, char*, size_t, struct segsum_entry*,
			   bool);

int lfs_write_helper(struct superblock*, struct inode_map*,
                     struct inode*, const char*, size_t, off_t);

#endif
