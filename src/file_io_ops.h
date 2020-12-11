#ifndef _FILE_IO_OPS_H_
#define _FILE_IO_OPS_H_

#include "380LFS.h"

#include <fuse.h>

int lfs_create(const char*, mode_t, struct fuse_file_info*);
int lfs_open(const char*, struct fuse_file_info*);
int lfs_read(const char*, char*, size_t, off_t, struct fuse_file_info*);
int lfs_write(const char*, const char*, size_t, off_t, struct fuse_file_info*);
int lfs_flush(const char*, struct fuse_file_info*);
int lfs_fsync(const char*, int, struct fuse_file_info*);
int lfs_release(const char*, struct fuse_file_info*);

#endif
