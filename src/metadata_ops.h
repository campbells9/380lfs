#ifndef _METADATA_OPS_H_
#define _METADATA_OPS_H_

#include <utime.h>
#include <sys/stat.h>
#include <sys/types.h>

int lfs_getattr(const char*, struct stat*);
int lfs_access(const char*, int);
int lfs_utime(const char*, struct utimbuf*);
int lfs_truncate(const char*, off_t);
int lfs_chmod(const char*, mode_t);
int lfs_chown(const char*, uid_t, gid_t);

#endif
