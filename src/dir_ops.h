#ifndef _DIR_OPS_H_
#define _DIR_OPS_H_

#include "380LFS.h"

#include <fuse.h>

int lfs_opendir(const char*, struct fuse_file_info*);
int lfs_readdir(const char*, void*, fuse_fill_dir_t, off_t,
                struct fuse_file_info*);
int lfs_mkdir(const char*, mode_t);
int lfs_rmdir(const char*);
int lfs_fsyncdir(const char*, int, struct fuse_file_info*);
int lfs_releasedir(const char*, struct fuse_file_info*);
int lfs_unlink(const char*);

#endif
