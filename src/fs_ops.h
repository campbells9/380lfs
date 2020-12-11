#ifndef _FS_OPS_H_
#define _FS_OPS_H_

#include "380LFS.h"

#include <fuse.h>
#include <sys/statvfs.h>

#define MIN_PROLOGUE_SIZE sizeof(off_t) + sizeof(int) * 3

void* lfs_init(struct fuse_conn_info*);
int lfs_statfs(const char*, struct statvfs*);
void lfs_destroy(void*);

#endif
