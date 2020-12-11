#include "metadata_helpers.h"
#include "segments.h"
#include "fs_ops.h"

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

struct superblock* get_superblock(
        struct superblock* sblock) {
    int fd = PRIVATE_DATA->fd;
    if(lseek(fd, 0, SEEK_SET) == -1) {
        fprintf(stderr, "failed to seek checkpoint region\n");

        return NULL;
    }
    
    if(read(fd, sblock, BLOCK_SIZE) < BLOCK_SIZE) {
        fprintf(stderr, "failed to read checkpoint region\n");
        
        return NULL;
    }
    return sblock;
}

int get_inumber(const char* path, struct superblock* sblock,
                struct inode_map* imap, struct inode* file) {
    // find root dir
    struct inode root;
    if(get_inode(ROOT_INUMBER, sblock, &root) == NULL) {
        return -1;
    }
    // search root dir entries for path
    struct dir_block dblock;
    int i = 0;
    int max_block = (int) root.statbuf.st_blocks;
    int entry_count = root.statbuf.st_size / sizeof(struct dir_entry);
    int entries_per_block = BLOCK_SIZE / sizeof(struct dir_entry);
    int entry, entry_max;
    int fd = PRIVATE_DATA->fd;
    while(i < max_block) {
        if(read_block(i, &root, (char*) &dblock) < BLOCK_SIZE) {
            fprintf(stderr, "failed to read block %d of root\n", i);

            return -1;
        }
        
        entry = 0;
        entry_max = entry_count - entries_per_block * i;
        if(entry_max > 16) {
            entry_max = 16;
        }
        while(entry < entry_max) {
            if(strcmp(dblock.entries[entry].name, path) == 0) {
                if(imap != NULL && get_imap(dblock.entries[entry].inumber, 
                                            sblock, imap) == NULL) {
                    return -1;
                }

                if(file != NULL && get_inode(dblock.entries[entry].inumber,
                                             sblock, file) == NULL) {
                    return -1;
                }

                return dblock.entries[entry].inumber;
            }
            
            entry++;
        }
        i++;
    }
    return -1;
}

struct inode* get_inode(int inumber, struct superblock* sblock, 
                        struct inode* file) {
    struct inode_map imap;
    if(get_imap(inumber, sblock, &imap) == NULL) {
        return NULL;
    }
    
    int fd = PRIVATE_DATA->fd;
    off_t inode_offset = imap.inode_blocks[INODE_TO_IMAP_INDEX(inumber)];
    if(lseek(fd, inode_offset, SEEK_SET) == -1) {
        fprintf(stderr, "failed to seek to inode %d\n", inumber);

        return NULL;
    }

    if(read(fd, file, sizeof(struct inode)) < sizeof(struct inode)) {
        fprintf(stderr, "failed to read inode %d\n", inumber);

        return NULL;
    }
    return file;
}

struct inode_map* get_imap(int inumber, struct superblock* sblock, 
                           struct inode_map* imap) {
    struct lfs_data* data = PRIVATE_DATA;
    int fd = data->fd;
    int imap_number = INODE_TO_IMAP(inumber);
    int max_imap_number = INODE_TO_IMAP(data->max_inumber);
    if(imap_number > max_imap_number) {
        // this imap isn't used yet, it has no offset
        imap->offset = (off_t) -1;

        return imap;
    }

    off_t imap_offset = sblock->inode_map_blocks[imap_number];
    if(lseek(fd, imap_offset, SEEK_SET) == -1) {
        fprintf(stderr, "Failed to seek imap for inode %d\n", inumber);

        return NULL;
    }


    if(read(fd, imap, BLOCK_SIZE) < BLOCK_SIZE) {
        fprintf(stderr, "Failed to read imap for inode %d\n", inumber);

        return NULL;
    }
    return imap;
}

int alloc_inumber(struct superblock* sblock) {
    int inumber = PRIVATE_DATA->max_inumber + 1;

    return inumber;
}

int commit_write(off_t tail, struct superblock* sblock) {
    int fd = PRIVATE_DATA->fd;
    PRIVATE_DATA->tail = tail;
    if(lseek(fd, 0, SEEK_SET) == -1) {
        fprintf(stderr, "failed to seek to checkpoint region\n");

        return -1;
    }

    if(write(fd, sblock, BLOCK_SIZE) < BLOCK_SIZE) {
        fprintf(stderr, "failed to write checkpoint region\n");

        return -1;
    }
    
    return 0;
}

int init_data(struct lfs_data* data) {
    int fd = data->fd;
    if(lseek(fd, BLOCK_SIZE, SEEK_SET) == -1) {
        return -1;
    }

    if(read(fd, &(data->tail), sizeof(off_t)) < sizeof(off_t)
            || read(fd, &(data->file_count), sizeof(int)) < sizeof(int)
            || read(fd, &(data->max_inumber), sizeof(int)) < sizeof(int)
            || read(fd, &(data->segment_count), sizeof(int)) < sizeof(int)
            || read(fd, &(data->clean_segments), sizeof(int)) < sizeof(int)) {
        return -1;
    }
    data->segsums = (struct segment_summary*) 
            calloc(data->segment_count, sizeof(struct segment_summary));
    if(data->segsums == NULL) {
        return -1;
    }

    size_t segsum_bytes = sizeof(struct segment_summary);
    for(int i = 0; i < data->segment_count; i++) {
        if(read(fd, &(data->segsums[i]), segsum_bytes) < segsum_bytes) {
            free(data->segsums);

            return -1;
        }
    }

    return 0;
}

int init_fh(struct inode* file, uint64_t* fh) {
    struct open_file* new_open = (struct open_file*) 
            malloc(sizeof(struct open_file));
    if(new_open == NULL) {
        fprintf(stderr, "malloc failed\n");

        return -1;
    }

    memcpy(&(new_open->file_inode), file, sizeof(struct inode));
    *fh = (uint64_t) new_open;

    return 0;
}

off_t* read_double_indirect(struct inode* file, 
                         off_t double_indirect_block[OFFSETS_PER_BLOCK]) {
    int fd = PRIVATE_DATA->fd;
    if(lseek(fd, file->double_indirect_block, SEEK_SET) == -1) {
        return NULL;
    }

    if(read(fd, double_indirect_block, BLOCK_SIZE) < BLOCK_SIZE) {
        return NULL;
    }

    return double_indirect_block;
}

off_t* read_indirect(off_t double_indirect_block[OFFSETS_PER_BLOCK], 
                     int block_no, off_t indirect_block[OFFSETS_PER_BLOCK]) {
    int fd = PRIVATE_DATA->fd;
    int di_index = DOUBLE_INDIRECT_INDEX(block_no);
    off_t block_offset = double_indirect_block[di_index];
    if(lseek(fd, block_offset, SEEK_SET) == -1) {
        return NULL;
    }

    if(read(fd, indirect_block, BLOCK_SIZE) < BLOCK_SIZE) {
        return NULL;
    }

    return indirect_block;
}

off_t get_block_offset(int block_no, struct inode* file) {
    if(block_no > file->statbuf.st_blocks) {
        fprintf(stderr, "invalid block number %d\n", block_no);

        return -1;
    }

    if(block_no < DIRECT_BLOCK_COUNT) {
        return file->direct_blocks[block_no];
    }

    off_t double_indirect[OFFSETS_PER_BLOCK];
    off_t indirect[OFFSETS_PER_BLOCK];
    if(read_double_indirect(file, double_indirect) == NULL
            || read_indirect(double_indirect, block_no, indirect) == NULL) {
        fprintf(stderr, "failed to read indirect blocks\n");

        return -1;
    }

    return indirect[INDIRECT_INDEX(block_no)];
}

int read_block(int block_no, struct inode* file, char buf[BLOCK_SIZE]) {
    int fd = PRIVATE_DATA->fd;
    off_t block_offset = get_block_offset(block_no, file);
    if(block_offset == -1) {
        return -1;
    }

    if(lseek(fd, block_offset, SEEK_SET) == -1) {
        fprintf(stderr, "failed to seek to block number %d\n", block_no);

        return -1;
    }
    return read(fd, buf, BLOCK_SIZE);
}

// read blocks [start, end] inclusive from file into buf, start <= end
int read_block_range(int start_block, int end_block, struct inode* file,
                     char* buf) {
    if(end_block >= file->statbuf.st_blocks) {
        fprintf(stderr, "invalid block numbers %d to %d\n", start_block, 
                end_block);

        return -1;
    }

    if(start_block > end_block) {
        return 0;
    }

    int fd = PRIVATE_DATA->fd;
    off_t double_indirect[OFFSETS_PER_BLOCK];
    off_t indirect[OFFSETS_PER_BLOCK];
    if(end_block >= DIRECT_BLOCK_COUNT) {
        if(read_double_indirect(file, double_indirect) == NULL) {
            fprintf(stderr, "failed to read double indirect block\n");

            return -1;
        }

        if(start_block >= DIRECT_BLOCK_COUNT 
                && INDIRECT_INDEX(start_block) > 0
                && read_indirect(double_indirect, start_block, 
                                 indirect) == NULL) {
            fprintf(stderr, "failed to read indirect blocks\n");

            return -1;
        }
    }

    off_t block_offset;
    int pos = 0;
    int bytes_read;
    int indirect_index;
    while(start_block <= end_block) {
        if(start_block < DIRECT_BLOCK_COUNT) {
            block_offset = file->direct_blocks[start_block];
        } else {
            indirect_index = INDIRECT_INDEX(start_block);
            if(indirect_index == 0 
                    && read_indirect(double_indirect, start_block, 
                                     indirect) == NULL) {
                fprintf(stderr, "failed to read indirect blocks\n");

                return -1;
            }

            block_offset = indirect[indirect_index];
        }
        if(lseek(fd, block_offset, SEEK_SET) == -1) {
            fprintf(stderr, "failed to seek to block number %d\n", start_block);

            return -1;
        }

        bytes_read = read(fd, buf + pos, BLOCK_SIZE);
        if(bytes_read < BLOCK_SIZE) {
            return -start_block;
        }

        pos += bytes_read;
        start_block++;
    }
    return pos;
}

int read_blocks_all(struct inode* file, char* buf) {
    return read_block_range(0, file->statbuf.st_blocks - 1, file, buf);
}

// size is a multiple of BLOCK_SIZE, size >= BLOCK_SIZE
// segsum_entries has an entry for every block in buffer
// size / BLOCK_SIZE == number of entries in segsum entries buffer
int log_append(struct superblock* sblock, char* buffer, size_t size,
               struct segsum_entry* segsum_entries, bool allow_clean) {
    struct lfs_data* data = PRIVATE_DATA;
    int fd = data->fd;
    off_t tail = data->tail;
    // update in-memory segment summaries for blocks about to be written
    int entry_count = size / BLOCK_SIZE;
    struct segment_summary* segsum;
    struct segsum_entry* entry_ptr;
    struct timespec update_time;
    if(clock_gettime(CLOCK_REALTIME, &update_time) == -1) {
        fprintf(stderr, "failed to read clock\n");

        return -1;
    }

    for(int entry = 0; entry < entry_count; entry++) {
        segsum = get_segsum(tail);
        entry_ptr = get_segsum_entry(tail);
        memcpy(entry_ptr, &(segsum_entries[entry]),
               sizeof(struct segsum_entry));
        if(segsum->live_bytes == 0) {
            data->clean_segments--;
        }
        segsum->live_bytes += BLOCK_SIZE;
        memcpy(&(segsum->last_write_time), &update_time,
               sizeof(struct timespec));
        if(lseek(fd, tail, SEEK_SET) == -1) {
            fprintf(stderr, "failed to seek to tail\n");

            return -1;
        }

        if(write(fd, buffer + (entry * BLOCK_SIZE), BLOCK_SIZE) < BLOCK_SIZE) {
            fprintf(stderr, "failed to write to tail\n");

            return -1;
        }

        tail = increment_tail(tail);
    }

    if(commit_write(tail, sblock) == -1) {
        return -1;
    }

    if(allow_clean && data->clean_segments < START_CLEAN_SEGMENT_THRESHOLD) {
        clean();
    }

    return 0;
}

int lfs_write_helper(struct superblock* sblock,
                     struct inode_map* imap, struct inode* file,
                     const char* buf, size_t size, off_t offset) {
    int inumber = (int) file->statbuf.st_ino;
    if(size == 0) {
        // empty write
        return 0;
    }
    
    if(offset > MAX_FILE_SIZE) {
        return -EFBIG;
    }

    if(offset + size > MAX_FILE_SIZE) {
        // only write what fits within max file size
        size = MAX_FILE_SIZE - offset;
    }
    
    // decide which blocks to read (which blocks are modified by write)
    off_t old_size = file->statbuf.st_size;
    off_t starting_point;
    if(offset < old_size) {
        starting_point = ROUND_DOWN_BLOCK(offset);
    } else {
        starting_point = ROUND_DOWN_BLOCK(old_size);
    }
    
    int blocks = (int) file->statbuf.st_blocks;
    int start_block = (int) starting_point / BLOCK_SIZE;
    int end_block = (int) (offset + size - 1) / BLOCK_SIZE;
    if(end_block >= MAX_BLOCK_COUNT) {
        end_block = MAX_BLOCK_COUNT - 1;
    }
    int modify_region_size = (end_block - start_block + 1) * BLOCK_SIZE;
    int indirect_region = 0;
    // add 2 blocks for new inode and imap
    int write_buffer_size = modify_region_size + 2 * BLOCK_SIZE;
    if(end_block >= DIRECT_BLOCK_COUNT) {
        // add a block for new double indirect
        write_buffer_size += BLOCK_SIZE;

        // add blocks for each rewritten indirect block
        int low_indirect = DOUBLE_INDIRECT_INDEX(start_block);
        int high_indirect = DOUBLE_INDIRECT_INDEX(end_block);
        if(low_indirect < 0) {
            low_indirect = 0;
        }
        indirect_region = (high_indirect - low_indirect + 1) * BLOCK_SIZE;
        write_buffer_size += indirect_region;
    }
    char* write_buffer = (char*) malloc(write_buffer_size);
    if(write_buffer == NULL) {
        fprintf(stderr, "malloc failed\n");
        
        return -1;
    }

    int last_block = end_block;
    if(last_block >= blocks) {
        last_block = blocks - 1;
    }
    int read_range = (last_block - start_block + 1) * BLOCK_SIZE;
    int read_result = read_block_range(start_block, last_block, file,
                                       write_buffer);
    if(read_result < read_range) {
        if(read_result <= 0) {
            fprintf(stderr, "failed to read block %d from inode %d\n",
                    -read_result, inumber);
        } else {
            fprintf(stderr, "failed to read blocks %d to %d from inode %d\n",
                    start_block, end_block, inumber);
        }
        free(write_buffer);

        return -1;
    }
    
    off_t tail = PRIVATE_DATA->tail;
    // store old offsets so they can be removed from segsums after write
    // segsum entries needed for log append
    int entry_count = write_buffer_size / BLOCK_SIZE;
    struct segsum_entry* entries = (struct segsum_entry*) 
            malloc(entry_count * sizeof(struct segsum_entry));
    if(entries == NULL) {
        fprintf(stderr, "malloc failed\n");
        free(write_buffer);
        
        return -1;
    }

    off_t* old_offsets = (off_t*) malloc(entry_count * sizeof(off_t));
    if(old_offsets == NULL) {
        fprintf(stderr, "malloc failed\n");
        free(write_buffer);
        free(entries);
        
        return -1;
    }

    int pos, entry_index, current_block;
    off_t* double_indirect;
    if(end_block >= DIRECT_BLOCK_COUNT) {
        pos = modify_region_size + indirect_region;
        entry_index = pos / BLOCK_SIZE;
        double_indirect = (off_t*) (write_buffer + pos);
        if(blocks > DIRECT_BLOCK_COUNT) {
            if(read_double_indirect(file, double_indirect) == NULL) {
                fprintf(stderr, "failed to read double indirect block\n");
                free(write_buffer);
                free(entries);
                free(old_offsets);

                return -1;
            }

            old_offsets[entry_index] = file->double_indirect_block;
        } else {
            old_offsets[entry_index] = (off_t) -1;
        }
        entries[entry_index].file_owner = inumber;
        entries[entry_index].file_offset = SEGSUM_DOUBLE_INDIRECT;
        pos = modify_region_size;
        entry_index = pos / BLOCK_SIZE;

        int d_ind_index, min_block;
        current_block = start_block;
        if(current_block < DIRECT_BLOCK_COUNT) {
            current_block = DIRECT_BLOCK_COUNT;
        }
        do {
            d_ind_index = DOUBLE_INDIRECT_INDEX(current_block);
            //ind_index = INDIRECT_INDEX(current_block);
            min_block = d_ind_index * OFFSETS_PER_BLOCK + DIRECT_BLOCK_COUNT;
            entries[entry_index].file_owner = inumber;
            entries[entry_index].file_offset = (d_ind_index + 1) 
                    * SEGSUM_INDIRECT;
            if(min_block < blocks) {
                old_offsets[entry_index] = double_indirect[d_ind_index];
                if(read_indirect(double_indirect, current_block, 
                                 (off_t*) (write_buffer + pos)) == NULL) {
                    fprintf(stderr, "failed to read indirect block\n");
                    free(write_buffer);
                    free(entries);
                    free(old_offsets);

                    return -1;
                }
            } else {
                old_offsets[entry_index] = (off_t) -1;
            }

            pos += BLOCK_SIZE;
            entry_index++;
            current_block += OFFSETS_PER_BLOCK;
        } while(current_block <= end_block);
    }

    off_t* indirect_ptr = (off_t*) (write_buffer + modify_region_size);
    current_block = start_block;
    if(current_block >= DIRECT_BLOCK_COUNT) {
        indirect_ptr += INDIRECT_INDEX(current_block);
    }
    entry_index = 0;
    do {
        entries[entry_index].file_owner = inumber;
        entries[entry_index].file_offset = (off_t) current_block * BLOCK_SIZE;
        if(current_block < DIRECT_BLOCK_COUNT) {
            if(current_block >= blocks) {
                old_offsets[entry_index] = (off_t) -1;
            } else {
                old_offsets[entry_index] = file->direct_blocks[current_block];
            }
            file->direct_blocks[current_block] = tail;
        } else {
            if(current_block >= blocks) {
                old_offsets[entry_index] = (off_t) -1;
            } else {
                old_offsets[entry_index] = *indirect_ptr;
            }
            *indirect_ptr = tail;
            indirect_ptr++;
        }
        tail = increment_tail(tail);
        entry_index++;
        current_block++;
    } while(current_block <= end_block);
    entry_index = entry_count - 2;
    old_offsets[entry_index] = file->offset;
    entries[entry_index].file_owner = inumber;
    entries[entry_index].file_offset = SEGSUM_METADATA;
    entry_index++;
    old_offsets[entry_index] = imap->offset;
    entries[entry_index].file_owner = SEGSUM_METADATA;
    entries[entry_index].file_offset = INODE_TO_IMAP(inumber);
    if(offset > old_size) {
        // pad file with zeroes to reach offset
        pos = (int) old_size - starting_point;
        memset(write_buffer + pos, 0, offset - old_size);
    } else {
        pos = (int) offset - starting_point;
    }
    memcpy(write_buffer + pos, buf, size);
    // update inode
    if(offset + size > old_size) {
        file->statbuf.st_size = offset + size;
        file->statbuf.st_blocks = end_block + 1;
    }
    pos = modify_region_size;
    if(end_block >= DIRECT_BLOCK_COUNT) {
        int start_di_index = DOUBLE_INDIRECT_INDEX(start_block);
        int end_di_index = DOUBLE_INDIRECT_INDEX(end_block);
        do {
            double_indirect[start_di_index] = tail;
            tail = increment_tail(tail);
            pos += BLOCK_SIZE;
            start_di_index++;
        } while(start_di_index <= end_di_index);
        file->double_indirect_block = tail;
        tail = increment_tail(tail);
        pos += BLOCK_SIZE;
    }
    // write inode to tail
    file->offset = tail;
    memcpy(write_buffer + pos, file, sizeof(struct inode));
    pos += BLOCK_SIZE;
    // update imap
    imap->inode_blocks[INODE_TO_IMAP_INDEX(inumber)] = tail;
    tail = increment_tail(tail);
    // write imap to tail
    imap->offset = tail;
    memcpy(write_buffer + pos, imap, BLOCK_SIZE);
    // update checkpoint region
    sblock->inode_map_blocks[INODE_TO_IMAP(inumber)] = tail;
    tail = increment_tail(tail);

    // complete write to tail
    int append_result = log_append(sblock, write_buffer, write_buffer_size,
                                   entries, true);
    free(write_buffer);
    free(entries);
    if(append_result == -1) {
        free(old_offsets);

        return -1;
    }

    // need the old offsets of blocks written:
    clear_segsum_entries(old_offsets, entry_count);
    free(old_offsets);
    
    return size;
}
