#include "380LFS.h"
#include "metadata_ops.h"
#include "metadata_helpers.h"

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

int lfs_getattr(const char* path, struct stat* statbuf) {
    memset(statbuf, 0, sizeof(struct stat));

    // find checkpoint region
    struct superblock sblock;
    struct inode file;
    if(get_superblock(&sblock) == NULL) {
        return -1;
    }
    int inumber = get_inumber(path, &sblock, NULL, &file);
    if(inumber == -1) {
        fprintf(stderr, "getattr: file %s not found\n", path);
        
        return -ENOENT;
    }

    memcpy(statbuf, &(file.statbuf), sizeof(struct stat));

    return 0;
}

int lfs_access(const char* path, int mask) {
    struct stat statbuf;
    int getattr_result = lfs_getattr(path, &statbuf);
    if(getattr_result < 0) {
        return getattr_result;
    }
    if(mask == F_OK) {
        return 0;
    }

    int test_mask = 0;
    if(mask & R_OK) {
        test_mask |= S_IRUSR;
    }
    if(mask & W_OK) {
        test_mask |= S_IWUSR;
    }
    if(mask & X_OK) {
        test_mask |= S_IXUSR;
    }
    if(statbuf.st_mode & test_mask == test_mask) {
        return 0;
    }

    return -EACCES;
}

int lfs_utime(const char* path, struct utimbuf* ubuf) {
    //TODO: error checking
    struct timespec access_timestamp, modify_timestamp;
    if(ubuf == NULL) {
        // set timestamps to current time
        if(clock_gettime(CLOCK_REALTIME, &access_timestamp) == -1) {
            fprintf(stderr, "utime: failed to read clock\n");

            return -1;
        }

        modify_timestamp.tv_sec = access_timestamp.tv_sec;
        modify_timestamp.tv_nsec = access_timestamp.tv_nsec;
    } else {
        access_timestamp.tv_sec = ubuf->actime;
        access_timestamp.tv_nsec = 0;
        modify_timestamp.tv_sec = ubuf->modtime;
        modify_timestamp.tv_nsec = 0;
    }
    
    struct superblock sblock;
    struct inode_map imap;
    struct inode file;
    if(get_superblock(&sblock) == NULL) {
        return -1;
    }
    int inumber = get_inumber(path, &sblock, &imap, &file);
    if(inumber == -1) {
        fprintf(stderr, "utime: file %s not found\n", path);

        return -1;
    }

    memcpy(&(file.statbuf.st_atim), &access_timestamp, sizeof(struct timespec));
    memcpy(&(file.statbuf.st_mtim), &modify_timestamp, sizeof(struct timespec));

    struct segsum_entry entries[2];
    off_t old_offsets[2];
    entries[0].file_owner = inumber;
    entries[0].file_offset = SEGSUM_METADATA;
    old_offsets[0] = file.offset;
    entries[1].file_owner = SEGSUM_METADATA;
    entries[1].file_offset = INODE_TO_IMAP(inumber);
    old_offsets[1] = imap.offset;
    
    char write_buffer[2 * BLOCK_SIZE];
    off_t tail = PRIVATE_DATA->tail;
    file.offset = tail;
    memcpy(write_buffer, &file, BLOCK_SIZE);
    imap.inode_blocks[INODE_TO_IMAP_INDEX(inumber)] = tail;
    tail = increment_tail(tail);
    imap.offset = tail;
    memcpy(write_buffer + BLOCK_SIZE, &imap, BLOCK_SIZE);
    sblock.inode_map_blocks[INODE_TO_IMAP(inumber)] = tail;
    tail = increment_tail(tail);

    if(log_append(&sblock, write_buffer, 2 * BLOCK_SIZE, entries, 
                  true) == -1) {
        return -1;
    }

    clear_segsum_entries(old_offsets, 2);

    return 0;
}

int lfs_truncate(const char* path, off_t new_size) {
    //TODO: error checking
    struct superblock sblock;
    struct inode_map imap;
    struct inode file;
    if(get_superblock(&sblock) == NULL) {
        return -1;
    }
    
    int inumber = get_inumber(path, &sblock, &imap, &file);
    if(inumber == -1) {
        fprintf(stderr, "truncate: file %s not found\n", path);

        return -1;
    }
    
    off_t old_size = file.statbuf.st_size;
    if(new_size > MAX_FILE_SIZE) {
        new_size = MAX_FILE_SIZE;
    }
    if(new_size == old_size) {
        return 0;
    }

    if(new_size > old_size) {
        // write zeroes until size is new_size
        int extend_size = new_size - old_size;
        char* zeroes = (char*) calloc(extend_size, sizeof(char));
        int bytes_written = lfs_write_helper(&sblock, &imap, &file, zeroes,
                                             extend_size, old_size);
        free(zeroes);
        if(bytes_written < extend_size) {
            return -1;
        }

        return 0;
    }

    struct segsum_entry entries[2];
    int old_offset_count = 2;
    int old_last_block = old_size / BLOCK_SIZE;
    int new_last_block = new_size / BLOCK_SIZE;
    if(new_last_block < old_last_block) {
        // need the old offsets of any blocks truncated away
        old_offset_count += old_last_block - new_last_block;
    }
    off_t* old_offsets = (off_t*) malloc(old_offset_count * sizeof(off_t));
    if(old_offsets == NULL) {
        fprintf(stderr, "truncate: malloc failed\n");

        return -1;
    }

    entries[0].file_owner = inumber;
    entries[0].file_offset = SEGSUM_METADATA;
    old_offsets[0] = file.offset;
    entries[1].file_owner = SEGSUM_METADATA;
    entries[1].file_offset = INODE_TO_IMAP(inumber);
    old_offsets[1] = imap.offset;
    if(new_last_block < old_last_block) {
        int current_block = new_last_block + 1;
        int offset_index = 2;
        while(current_block <= old_last_block) {
            old_offsets[offset_index] = get_block_offset(current_block, &file);
            current_block++;
            offset_index++;
        }
    }
    
    char write_buffer[2 * BLOCK_SIZE];
    off_t tail = PRIVATE_DATA->tail;
    file.statbuf.st_blocks = new_size / BLOCK_SIZE + 1;
    file.statbuf.st_size = new_size;
    
    int pos = 0;
    // write inode to tail
    file.offset = tail;
    memcpy(write_buffer + pos, &file, sizeof(struct inode));
    pos += BLOCK_SIZE;

    // update imap
    imap.inode_blocks[INODE_TO_IMAP_INDEX(inumber)] = tail;
    tail = increment_tail(tail);
    // write imap to tail
    imap.offset = tail;
    memcpy(write_buffer + pos, &imap, BLOCK_SIZE);
    // update checkpoint region
    sblock.inode_map_blocks[INODE_TO_IMAP(inumber)] = tail;
    tail = increment_tail(tail);

    // complete write to tail
    if(log_append(&sblock, write_buffer, 2 * BLOCK_SIZE, entries, 
                  true) == -1) {
        free(old_offsets);

        return -1;
    }

    clear_segsum_entries(old_offsets, old_offset_count);
    free(old_offsets);

    return 0;
}
