#include "380LFS.h"
#include "metadata_helpers.h"
#include "file_io_ops.h"
#include "metadata_ops.h"

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

int lfs_opendir(const char* path, struct fuse_file_info* fi) {
    return lfs_open("/", fi);
}

int lfs_releasedir(const char* path, struct fuse_file_info* fi) {
    return lfs_release("/", fi);
}
