#include "380LFS.h"
#include "fs_ops.h"
#include "metadata_helpers.h"
#include "segments.h"

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void* lfs_init(struct fuse_conn_info* conn) {
    struct lfs_data* data = PRIVATE_DATA;
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH; // rw-r--r--
    data->fd = open(data->log_name, O_CREAT | O_RDWR, mode);
    data->segment_count = data->log_size / SEGMENT_SIZE;

    struct stat statbuf;
    if(fstat(data->fd, &statbuf) == -1) {
        fprintf(stderr, "init: unable to open log file %s\n", data->log_name);
        
        exit(-1);
    }
    
    off_t end = statbuf.st_size;
    if(end > 0) {
        // log already exists
        data->log_size = end;
        if(init_data(data) == -1) {
            fprintf(stderr, "init: unable to load metadata\n");

            exit(-1);
        }
        
        return data;
    }

    if(ftruncate(data->fd, data->log_size) == -1) {
        fprintf(stderr, "init: failed to extend log file %s to %ld bytes\n",
                data->log_name, data->log_size);
                
        exit(-1);
    }

    // create checkpoint region and root inode
    struct superblock sblock;
    struct inode_map imap;
    struct inode root;
    sblock.segment_size = SEGMENT_SIZE;
    sblock.block_size = BLOCK_SIZE;
    memcpy(&(root.statbuf), &statbuf, sizeof(struct stat));
    root.statbuf.st_ino = ROOT_INUMBER;
    // same permissions as log file +x, and as a directory
    mode = statbuf.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);
    mode |= S_IXUSR | S_IXGRP | S_IXOTH;
    root.statbuf.st_mode = S_IFDIR | mode;
    root.statbuf.st_nlink = 1;
    root.statbuf.st_size = 2 * sizeof(struct dir_entry);
    root.statbuf.st_blksize = BLOCK_SIZE;
    root.statbuf.st_blocks = 1;
    struct dir_entry root_entries[2];
    root_entries[0].inumber = ROOT_INUMBER;
    strncpy(root_entries[0].name, "/", MAX_FILENAME); //.
    root_entries[1].inumber = ROOT_INUMBER;
    strncpy(root_entries[1].name, "/", MAX_FILENAME); //..
    
    // prepare the prologue, first segments which contain all segment info
    int prologue_bytes = MIN_PROLOGUE_SIZE;
    prologue_bytes += sizeof(struct segment_summary) * data->segment_count;

    int prologue_segments = prologue_bytes / SEGMENT_SIZE + 1;
    off_t prologue_end = prologue_segments * SEGMENT_SIZE;
    int log_buffer_size = (int) prologue_end + 3 * BLOCK_SIZE;
    char* log_buffer = (char*) malloc(log_buffer_size);
    if(log_buffer == NULL) {
        fprintf(stderr, "init: malloc failed\n");

        exit(-1);
    }
    int pos = 0;
    sblock.inode_map_blocks[INODE_TO_IMAP(ROOT_INUMBER)] = prologue_end;
    memcpy(log_buffer + pos, &sblock, BLOCK_SIZE);
    pos += (int) prologue_end;
    // write first imap
    imap.offset = (off_t) pos;
    imap.inode_blocks[INODE_TO_IMAP_INDEX(ROOT_INUMBER)] = pos + BLOCK_SIZE;
    memcpy(log_buffer + pos, &imap, BLOCK_SIZE);
    pos += BLOCK_SIZE;

    // write root inode
    root.offset = (off_t) pos;
    root.direct_blocks[0] = pos + BLOCK_SIZE;
    memcpy(log_buffer + pos, &root, BLOCK_SIZE);
    pos += BLOCK_SIZE;

    // write root data
    memcpy(log_buffer + pos, &root_entries, BLOCK_SIZE);
    pos += BLOCK_SIZE;

    // log: IMAP 0 | INODE 0 | INODE 0 DATA 0
    if(write(data->fd, log_buffer, log_buffer_size) < log_buffer_size) {
        fprintf(stderr, "init: can't initialize log file %s\n", data->log_name);

        exit(-1);
    }

    free(log_buffer);
    data->tail = log_buffer_size;
    data->file_count = ROOT_INUMBER + 1;
    data->max_inumber = 0;
    data->clean_segments = data->segment_count - (prologue_segments + 1);
    data->segsums = (struct segment_summary*) 
            calloc(data->segment_count, sizeof(struct segment_summary));
    if(data->segsums == NULL) {
        fprintf(stderr, "init: malloc failed\n");

        exit(-1);
    }

    int first_segment = prologue_segments + 1;
    data->segsums[first_segment].live_bytes = log_buffer_size - SEGMENT_SIZE;
    // entries[0] is imap 0
    data->segsums[first_segment].entries[0].file_owner = SEGSUM_METADATA;
    data->segsums[first_segment].entries[0].file_offset = 0;
    // entries[1] is inode 0
    data->segsums[first_segment].entries[1].file_owner = SEGSUM_ROOT;
    data->segsums[first_segment].entries[1].file_offset = SEGSUM_METADATA;
    // entries[2] is file 0 at offset 0
    data->segsums[first_segment].entries[2].file_owner = SEGSUM_ROOT;
    data->segsums[first_segment].entries[2].file_offset = 0;
    // update write times
    if(clock_gettime(CLOCK_REALTIME,
                     &(data->segsums[first_segment].last_write_time)) == -1) {
        fprintf(stderr, "init: failed to read clock\n");
        free(data->segsums);

        exit(-1);
    }

    for(int seg = 0; seg < prologue_segments; seg++) {
        data->segsums[seg].live_bytes = SEGMENT_SIZE;
        // segment 0 is all metadata: write SEGSUM_METADATA, whole segment is
        // always alive
        memset(&(data->segsums[seg].entries), SEGSUM_METADATA,
               BLOCKS_PER_SEGMENT * sizeof(struct segsum_entry));
        memcpy(&(data->segsums[first_segment].last_write_time),
               &(data->segsums[seg].last_write_time), sizeof(struct timespec));
    }

    return data;
}

int lfs_statfs(const char *path, struct statvfs *statv) {
    statv->f_bsize = BLOCK_SIZE;
    statv->f_blocks = PRIVATE_DATA->segment_count * BLOCKS_PER_SEGMENT;
    statv->f_files = PRIVATE_DATA->file_count;
    statv->f_namemax = MAX_FILENAME;

    return 0;
}

void lfs_destroy(void* private_data) {
    // write in-memory status of segments to prologue so it can be recovered
    // next time backing file is mounted
    struct lfs_data* data = (struct lfs_data*) private_data;
    int fd = data->fd;
    if(lseek(fd, BLOCK_SIZE, SEEK_SET) == -1) {
        return;
    }

    if(write(fd, &(data->tail), sizeof(off_t)) < sizeof(off_t)
            || write(fd, &(data->file_count), sizeof(int)) < sizeof(int)
            || write(fd, &(data->max_inumber), sizeof(int)) < sizeof(int)
            || write(fd, &(data->segment_count), sizeof(int)) < sizeof(int)
            || write(fd, &(data->clean_segments), sizeof(int)) < sizeof(int)) {
        return;
    }

    size_t segsum_bytes = sizeof(struct segment_summary);
    for(int i = 0; i < data->segment_count; i++) {
        if(write(fd, &(data->segsums[i]), segsum_bytes) < segsum_bytes) {
            free(data->segsums);

            return;
        }
    }

    free(data->segsums);
    close(fd);
}
