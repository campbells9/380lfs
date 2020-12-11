#ifndef _LINK_OPS_H_
#define _LINK_OPS_H_

#include <stddef.h>

int lfs_link(const char*, const char*);
int lfs_unlink(const char*);
int lfs_readlink(const char*, char*, size_t);
int lfs_symlink(const char*, const char*);
int lfs_rename(const char*, const char*);

#endif
