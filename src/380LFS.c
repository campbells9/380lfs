#include "380LFS.h"
#include "metadata_helpers.h"
#include "file_io_ops.h"
#include "dir_ops.h"
#include "metadata_ops.h"
#include "fs_ops.h"
#include "link_ops.h"

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>

struct fuse_operations lfs_oper = {
    .init = lfs_init,
    .getattr = lfs_getattr,
    .access = lfs_access,
    .create = lfs_create,
    .utime = lfs_utime,
    .truncate = lfs_truncate,
    .unlink = lfs_unlink,
    .open = lfs_open,
    .read = lfs_read,
    .write = lfs_write,
    .fsync = lfs_fsync,
    .flush = lfs_flush,
    .release = lfs_release,
    .opendir = lfs_opendir,
    .releasedir = lfs_releasedir,
    .statfs = lfs_statfs,
    .destroy = lfs_destroy
};

int main(int argc, char* argv[]) {
    if(argc < 4) {
        fprintf(stderr, 
            "Usage: %s -s [FUSE OPTS]... [MOUNTDIR] [LOGFILE] [SIZE (GB)]\n",
                argv[0]);
        
        return 1;
    }
    
    struct lfs_data* data = (struct lfs_data*) malloc(sizeof(struct lfs_data));
    if(data == NULL) {
        fprintf(stderr, "malloc failed");

        return -1;
    }

    data->log_name = argv[argc - 2];
    data->log_size = (off_t) (atoi(argv[argc - 1]) * GB);
    argc -= 2;
    argv[argc] = NULL;
    
    return fuse_main(argc, argv, &lfs_oper, data);
}
