#include "file_io_ops.h"
#include "metadata_helpers.h"

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

int lfs_create(const char* path, mode_t mode, struct fuse_file_info* fi) {
    struct superblock sblock;
    if(get_superblock(&sblock) == NULL) {
        return -1;
    }

    int inumber = alloc_inumber(&sblock);
    if(inumber >= MAX_INUMBER) {
        fprintf(stderr, "create: cannot allocate an inumber for %s\n", path);
        
        return -1;
    }
    
    PRIVATE_DATA->file_count++;
    
    struct inode_map root_imap;
    struct inode root;
    if(get_imap(ROOT_INUMBER, &sblock, &root_imap) == NULL
            || get_inode(ROOT_INUMBER, &sblock, &root) == NULL) {
        return -1;
    }
    
    struct dir_entry d_entry;
    d_entry.inumber = inumber;
    strncpy(d_entry.name, path, MAX_FILENAME);
    if(lfs_write_helper(&sblock, &root_imap, &root, (char*) &d_entry, 
                        sizeof(struct dir_entry), 
                        root.statbuf.st_size) < sizeof(struct dir_entry)) {
        return -1;
    }

    // create new inode for new file
    struct inode_map new_file_imap;
    struct inode new_file;
    if(get_imap(inumber, &sblock, &new_file_imap) == NULL) {
        return -1;
    }

    memcpy(&(new_file.statbuf), &(root.statbuf), sizeof(struct stat));
    new_file.statbuf.st_ino = inumber;
    new_file.statbuf.st_mode = mode;
    new_file.statbuf.st_nlink = 1;
    new_file.statbuf.st_size = 0;
    new_file.statbuf.st_blocks = 0;
    
    // new inode, new root, new root inode, (new file imap), root imap
    char write_buffer[2 * BLOCK_SIZE];
    // 4 or 5 blocks
    struct segsum_entry entries[2];
    off_t old_offsets[2];
    // new inode
    entries[0].file_owner = inumber;
    entries[0].file_offset = SEGSUM_METADATA;
    old_offsets[0] = -1;
    // new file imap
    entries[1].file_owner = SEGSUM_METADATA;
    entries[1].file_offset = INODE_TO_IMAP(inumber);
    old_offsets[1] = new_file_imap.offset;

    int pos = 0;
    off_t tail = PRIVATE_DATA->tail;
    new_file.offset = tail;
    memcpy(write_buffer + pos, &new_file, BLOCK_SIZE);
    pos += BLOCK_SIZE;
    new_file_imap.inode_blocks[INODE_TO_IMAP_INDEX(inumber)] = tail;
    tail = increment_tail(tail);
    new_file_imap.offset = tail;
    memcpy(write_buffer + pos, &new_file_imap, BLOCK_SIZE);
    pos += BLOCK_SIZE;
    sblock.inode_map_blocks[INODE_TO_IMAP(inumber)] = tail;
    tail = increment_tail(tail);
    if(log_append(&sblock, write_buffer, 2 * BLOCK_SIZE, entries, 
                  true) == -1) {
        return -1;
    }

    clear_segsum_entries(old_offsets, 2);

    return init_fh(&new_file, &(fi->fh));
}

int lfs_open(const char* path, struct fuse_file_info* fi) {
    struct superblock sblock;
    struct inode file;
    if(get_superblock(&sblock) == NULL) {
        return -1;
    }
    
    int inumber = get_inumber(path, &sblock, NULL, &file);
    if(inumber == -1) {
        fprintf(stderr, "open: file %s not found\n", path);

        return -1;
    }
    
    return init_fh(&file, &(fi->fh));
}

int lfs_read(const char* path, char* buf, size_t size, off_t offset,
             struct fuse_file_info* fi) {
    //TODO: all error checking
    struct superblock sblock;
    if(get_superblock(&sblock) == NULL) {
        return -1;
    }
    
    struct open_file* file = (struct open_file*) fi->fh;
    int inumber = file->file_inode.statbuf.st_ino;
    if(get_inode(inumber, &sblock, &(file->file_inode)) == NULL) {
        return -1;
    }

    if(size == 0 || offset >= file->file_inode.statbuf.st_size) {
        return 0;
    }
    
    int fd = PRIVATE_DATA->fd;
    int current_block = (int) offset / BLOCK_SIZE;
    int end_block = (int) (offset + size - 1) / BLOCK_SIZE;
    if(end_block >= MAX_BLOCK_COUNT) {
        end_block = MAX_BLOCK_COUNT - 1;
    }

    int read_buffer_size = (end_block - current_block + 1) * BLOCK_SIZE;
    char* read_buffer = (char*) malloc(read_buffer_size);
    if(read_buffer == NULL) {
        fprintf(stderr, "read: malloc failed\n");

        return -1;
    }

    int read_result = read_block_range(current_block, end_block,
                                       &(file->file_inode), read_buffer);
    if(read_result < read_buffer_size) {
        if(read_result <= 0) {
            fprintf(stderr, "failed to read block %d of file %s\n",
                    -read_result, path);
        } else {
            fprintf(stderr, "failed to read blocks %d to %d of file %s\n",
                    current_block, end_block, path);
        }
        free(read_buffer);

        return -1;
    }

    off_t starting_point = ROUND_DOWN_BLOCK(offset);
    memcpy(buf, read_buffer + (offset - starting_point), size);
    free(read_buffer);

    return size;
}

int lfs_write(const char* path, const char* buf, size_t size, off_t offset,
              struct fuse_file_info* fi) {
    struct superblock sblock;
    if(get_superblock(&sblock) == NULL) {
        return -1;
    }
    struct open_file* file = (struct open_file*) fi->fh;
    struct inode_map imap;
    int inumber = file->file_inode.statbuf.st_ino;
    if(get_inode(inumber, &sblock, &(file->file_inode)) == NULL
            || get_imap(inumber, &sblock, &imap) == NULL) {
        return -1;
    }

    return lfs_write_helper(&sblock, &imap, &(file->file_inode), buf, size,
                            offset);
}

int lfs_flush(const char *path, struct fuse_file_info *fi) {
    return 0;
}

int lfs_fsync(const char* path, int datasync, struct fuse_file_info* fi) {
    return 0;
}

int lfs_release(const char* path, struct fuse_file_info* fi) {
    free((struct open_file*) fi->fh);

    return 0;
}
