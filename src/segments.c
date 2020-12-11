#include "380LFS.h"
#include "metadata_helpers.h"
#include "segments.h"

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

struct segsum_sort_entry {
    int segment_number;
    double benefit_cost_ratio;
    struct segment_summary summary;
};

struct d_ind_table_entry {
    off_t* original;
    off_t** indirects;
};

int compare_segments(const void* entry1, const void* entry2) {
    struct segsum_sort_entry* seg1 = (struct segsum_sort_entry*) entry1;
    struct segsum_sort_entry* seg2 = (struct segsum_sort_entry*) entry2;
    // clean segments go to the back of the list
    if(seg1->summary.live_bytes == 0) {
        return 1;
    }

    if(seg2->summary.live_bytes == 0) {
        return -1;
    }

    return (int) (seg2->benefit_cost_ratio - seg1->benefit_cost_ratio);
}

void free_tables(struct inode_map** imap_table, int imap_table_len,
                 struct inode** file_table, int file_table_len,
                 struct d_ind_table_entry* d_ind_table,
                 char** datablock_table, int datablock_count) {
    int i, j;
    for(i = 0; i < imap_table_len; i++) {
        free(imap_table[i]);
    }
    free(imap_table);
    for(i = 0; i < file_table_len; i++) {
        free(d_ind_table[i].original);
        for(j = 0; j < OFFSETS_PER_BLOCK; j++) {
            free(d_ind_table[i].indirects[j]);
        }
        free(d_ind_table[i].indirects);
        free(d_ind_table + i);
        free(file_table[i]);
    }
    free(file_table);
    for(i = 0; i < datablock_count; i++) {
        free(datablock_table[i]);
    }
}

// Mr. Clean gets tough on cold segments
void clean() {
    struct lfs_data* data = PRIVATE_DATA;
    struct superblock sblock;
    if(get_superblock(&sblock) == NULL) {
        return;
    }

    size_t segsum_sorted_len = data->segment_count 
            * sizeof(struct segsum_sort_entry);
    //int d_ind_count;
    int seg, total_imap_count, imap_count, file_count, datablock_count, block;
    int file_owner, imap_number, imap_index, new_block_count, old_offset_index;
    int pos, datablock, inumber, append_status, d_ind_count, ind_count, i;
    int d_ind_index, block_no;
    off_t file_offset, old_tail, tail, *old_offsets;
    struct segsum_sort_entry* segsum_sort_array, *entry_ptr;
    struct segment_summary* dirty_segsum;
    struct inode_map** imap_table, *imap;
    struct inode** file_table, *file;
    struct d_ind_table_entry* d_ind_table;
    char* datablock_table[BLOCKS_PER_SEGMENT * SEGMENTS_PER_CLEAN];
    char* data_block, *append_buffer;
    struct segsum_entry* entry_table[BLOCKS_PER_SEGMENT * SEGMENTS_PER_CLEAN];
    struct segsum_entry* append_entries;
    double utilization, timestamp, age, benefit, cost;
    struct timespec reference_time;
    if(clock_gettime(CLOCK_REALTIME, &reference_time) == -1) {
        fprintf(stderr, "cleaning error: failed to read clock\n");

        return;
    }

    double current_seconds = reference_time.tv_sec 
            + reference_time.tv_nsec / NSEC_PER_SEC;
    while(data->clean_segments < STOP_CLEAN_SEGMENT_THRESHOLD) {
        segsum_sort_array = (struct segsum_sort_entry*) 
                malloc(segsum_sorted_len);
        if(segsum_sort_array == NULL) {
            fprintf(stderr, "cleaning error: malloc failed\n");

            return;
        }

        for(seg = 0; seg < data->segment_count; seg++) {
            segsum_sort_array[seg].segment_number = seg;
            memcpy(&(segsum_sort_array[seg].summary), &(data->segsums[seg]),
                   sizeof(struct segment_summary));
            utilization = (double) data->segsums[seg].live_bytes / SEGMENT_SIZE;
            timestamp = data->segsums[seg].last_write_time.tv_sec 
                    + data->segsums[seg].last_write_time.tv_nsec / NSEC_PER_SEC;
            age = current_seconds - timestamp;
            benefit = (1 - utilization) * age;
            cost = 1 + utilization;
            segsum_sort_array[seg].benefit_cost_ratio = benefit / cost;
        }
        qsort(segsum_sort_array, data->segment_count, 
              sizeof(struct segsum_sort_entry), compare_segments);
        total_imap_count = INODE_TO_IMAP(data->file_count) + 1;
        imap_table = (struct inode_map**) calloc(total_imap_count, 
                                                 sizeof(struct inode_map*));
        imap_count = 0;
        file_table = (struct inode**) calloc(data->file_count,
                                             sizeof(struct inode*));
        file_count = 0;
        d_ind_table = (struct d_ind_table_entry*) 
                calloc(data->file_count, sizeof(struct d_ind_table_entry));
        d_ind_count = 0;
        ind_count = 0;
        datablock_count = 0;
        if(imap_table == NULL || file_table == NULL || d_ind_table == NULL) {
            fprintf(stderr, "cleaning error: calloc failed\n");
            free(segsum_sort_array);
            free(imap_table);
            free(file_table);
            free(d_ind_table);

            return;
        }

        for(seg = 0; seg < SEGMENTS_PER_CLEAN; seg++) {
            entry_ptr = segsum_sort_array + seg;
            dirty_segsum = &(entry_ptr->summary);
            for(block = 0; block < BLOCKS_PER_SEGMENT; block++) {
                file_owner = dirty_segsum->entries[block].file_owner;
                file_offset = dirty_segsum->entries[block].file_offset;
                if(file_owner != 0) {
                    if(file_owner == SEGSUM_ROOT) {
                        file_owner = 0;
                    }
                    if(file_owner == SEGSUM_METADATA) {
                        imap_number = (int) file_offset;
                    } else {
                        imap_number = INODE_TO_IMAP(file_owner);
                    }
                    if(imap_table[imap_number] == NULL) {
                        imap = (struct inode_map*) 
                                malloc(sizeof(struct inode_map));
                        if(imap == NULL 
                                || get_imap(imap_number * (OFFSETS_PER_BLOCK),
                                            &sblock, imap) == NULL) {
                            free(segsum_sort_array);
                            free_tables(imap_table, total_imap_count, 
                                        file_table, data->file_count, 
                                        d_ind_table,
                                        datablock_table, datablock_count);

                            return;
                        }

                        imap_table[imap_number] = imap;
                        imap_count++;
                    }
                    if(file_owner != SEGSUM_METADATA) {
                        if(file_table[file_owner] == NULL) {
                            file = (struct inode*) malloc(sizeof(struct inode));
                            if(file == NULL || get_inode(file_owner, &sblock,
                                                         file) == NULL) {
                                free(segsum_sort_array);
                                free_tables(imap_table, total_imap_count,
                                            file_table, data->file_count,
                                            d_ind_table,
                                            datablock_table, datablock_count);

                                return;
                            }

                            file_table[file_owner] = file;
                            file_count++;
                        }
                        if(file_offset <= SEGSUM_DOUBLE_INDIRECT 
                                || file_offset >= MIN_INDIRECT_OFFSET) {
                            // double indirect block
                            if(d_ind_table[file_owner].original == NULL) {
                                d_ind_table[file_owner].original = (off_t*) 
                                        malloc(BLOCK_SIZE);
                                d_ind_table[file_owner].indirects = (off_t**) 
                                        calloc(OFFSETS_PER_BLOCK, 
                                               sizeof(off_t*));
                                if(d_ind_table[file_owner].original == NULL 
                                        || d_ind_table[file_owner].indirects 
                                            == NULL
                                        || read_double_indirect(
                                                file_table[file_owner], 
                                                d_ind_table[file_owner].original
                                            ) == NULL) {
                                    free(segsum_sort_array);
                                    free_tables(imap_table, total_imap_count,
                                                file_table, data->file_count,
                                                d_ind_table,
                                                datablock_table, 
                                                datablock_count);

                                    return;
                                }
                                
                                d_ind_count++;
                            }
                        }
                        if(file_offset <= SEGSUM_INDIRECT 
                                || file_offset >= MIN_INDIRECT_OFFSET) {
                            d_ind_index = file_offset / SEGSUM_INDIRECT - 1;
                            block_no = d_ind_index * OFFSETS_PER_BLOCK 
                                    + DIRECT_BLOCK_COUNT;
                            if(d_ind_table[file_owner].indirects[d_ind_index] 
                                    == NULL) {
                                d_ind_table[file_owner].indirects[d_ind_index] =
                                        (off_t*) malloc(BLOCK_SIZE);
                                if(d_ind_table[file_owner]
                                        .indirects[d_ind_index] == NULL
                                    || read_indirect(
                                            d_ind_table[file_owner].original, 
                                            block_no, 
                                            d_ind_table[file_owner]
                                                    .indirects[d_ind_index]
                                        ) == NULL) {
                                    free(segsum_sort_array);
                                    free_tables(imap_table, total_imap_count,
                                                file_table, data->file_count,
                                                d_ind_table,
                                                datablock_table, 
                                                datablock_count);

                                    return;
                                }

                                ind_count++;
                            }
                        }
                        if(file_offset >= 0) {
                            data_block = (char*) malloc(BLOCK_SIZE);
                            if(data_block == NULL 
                                    || read_block(file_offset / BLOCK_SIZE, 
                                                  file_table[file_owner], 
                                                  data_block) < BLOCK_SIZE) {
                                free(segsum_sort_array);
                                free_tables(imap_table, total_imap_count, 
                                            file_table, data->file_count, 
                                            d_ind_table,
                                            datablock_table, datablock_count);

                                return;
                            }

                            datablock_table[datablock_count] = data_block;
                            entry_table[datablock_count] = 
                                    &(dirty_segsum->entries[block]);
                            datablock_count++;
                        }
                    }
                }
            }
        }
        free(segsum_sort_array);
        old_tail = find_next_clean_segment(data->tail);
        if(old_tail == (off_t) -1) {
            fprintf(stderr, "cleaning error: no clean segments left\n");
            free_tables(imap_table, total_imap_count, file_table, 
                        data->file_count, d_ind_table, 
                        datablock_table, datablock_count);

            return;
        }

        tail = old_tail;
        new_block_count = datablock_count + d_ind_count + ind_count + file_count
                + imap_count;
        old_offsets = (off_t*) malloc(new_block_count * sizeof(off_t));
        old_offset_index = 0;
        append_buffer = (char*) malloc(new_block_count * BLOCK_SIZE);
        append_entries = (struct segsum_entry*) 
                malloc(new_block_count * sizeof(struct segsum_entry));
        pos = 0;
        if(old_offsets == NULL || append_buffer == NULL 
                || append_entries == NULL) {
            fprintf(stderr, "cleaning error: malloc failed\n");
            free_tables(imap_table, total_imap_count, file_table, 
                        data->file_count, d_ind_table, 
                        datablock_table, datablock_count);
            free(old_offsets);
            free(append_buffer);
            free(append_entries);

            return;
        }

        for(datablock = 0; datablock < datablock_count; datablock++) {
            memcpy(append_buffer + pos, datablock_table[datablock], BLOCK_SIZE);
            file_owner = entry_table[datablock]->file_owner;
            block = entry_table[datablock]->file_offset / BLOCK_SIZE;
            if(block < DIRECT_BLOCK_COUNT) {
                old_offsets[old_offset_index] = 
                        file_table[file_owner]->direct_blocks[block];
            } else {
                old_offsets[old_offset_index] = d_ind_table[file_owner]
                        .indirects[DOUBLE_INDIRECT_INDEX(block)]
                                  [INDIRECT_INDEX(block)];
            }
            append_entries[old_offset_index].file_owner = file_owner;
            append_entries[old_offset_index].file_offset = 
                    entry_table[datablock]->file_offset;
            old_offset_index++;
            file_table[file_owner]->direct_blocks[block] = tail;
            tail = increment_tail(tail);
            pos += BLOCK_SIZE;
        }
        for(inumber = 0; inumber < data->file_count; inumber++) {
            if(d_ind_table[inumber].indirects != NULL) {
                for(i = 0; i < OFFSETS_PER_BLOCK; i++) {
                    if(d_ind_table[inumber].indirects[i] != NULL) {
                        memcpy(append_buffer + pos, 
                               d_ind_table[inumber].indirects[i], BLOCK_SIZE);
                        old_offsets[old_offset_index] = 
                                d_ind_table[inumber].original[i];
                        if(inumber == 0) {
                            append_entries[old_offset_index].file_owner = 
                                    SEGSUM_ROOT;
                        } else {
                            append_entries[old_offset_index].file_owner = 
                                    inumber;
                        }
                        append_entries[old_offset_index].file_offset = (i + 1) 
                                * SEGSUM_INDIRECT;
                        old_offset_index++;
                        d_ind_table[inumber].original[i] = tail;
                        tail = increment_tail(tail);
                        pos += BLOCK_SIZE;
                    }
                }
                memcpy(append_buffer + pos, d_ind_table[inumber].original, 
                       BLOCK_SIZE);
                old_offsets[old_offset_index] = 
                        file_table[inumber]->double_indirect_block;
                if(inumber == 0) {
                    append_entries[old_offset_index].file_owner = SEGSUM_ROOT;
                } else {
                    append_entries[old_offset_index].file_owner = inumber;
                }
                append_entries[old_offset_index].file_offset = 
                        SEGSUM_DOUBLE_INDIRECT;
                old_offset_index++;
                file_table[inumber]->double_indirect_block = tail;
                tail = increment_tail(tail);
                pos += BLOCK_SIZE;
            }
            if(file_table[inumber] != NULL) {
                memcpy(append_buffer + pos, file_table[inumber], 
                       sizeof(struct inode));
                imap_number = INODE_TO_IMAP(inumber);
                imap_index = INODE_TO_IMAP_INDEX(inumber);
                old_offsets[old_offset_index] = 
                        imap_table[imap_number]->inode_blocks[imap_index];
                if(inumber == 0) {
                    append_entries[old_offset_index].file_owner = SEGSUM_ROOT;
                } else {
                    append_entries[old_offset_index].file_owner = inumber;
                }
                append_entries[old_offset_index].file_offset = SEGSUM_METADATA;
                old_offset_index++;
                imap_table[imap_number]->inode_blocks[imap_index] = tail;
                tail = increment_tail(tail);
                pos += BLOCK_SIZE;
            }
        }
        for(imap_number = 0; imap_number < total_imap_count; imap_number++) {
            if(imap_table[imap_number] != NULL) {
                memcpy(append_buffer + pos, imap_table[imap_number], 
                       sizeof(struct inode_map));
                old_offsets[old_offset_index] = 
                        sblock.inode_map_blocks[imap_number];
                append_entries[old_offset_index].file_owner = SEGSUM_METADATA;
                append_entries[old_offset_index].file_offset = imap_number;
                old_offset_index++;
                sblock.inode_map_blocks[imap_number] = tail;
                tail = increment_tail(tail);
                pos += BLOCK_SIZE;
            }
        }
        free_tables(imap_table, total_imap_count, file_table, 
                    data->file_count, d_ind_table,
                    datablock_table, datablock_count);
        data->tail = old_tail;
        append_status = log_append(&sblock, append_buffer, 
                                   new_block_count * BLOCK_SIZE, 
                                   append_entries, false);
        free(append_buffer);
        free(append_entries);
        if(append_status == -1) {
            free(old_offsets);

            return;
        }

        clear_segsum_entries(old_offsets, new_block_count);
        free(old_offsets);
    }
}

off_t find_next_clean_segment(off_t tail) {
    struct lfs_data* data = PRIVATE_DATA;
    if(data->clean_segments == 0) {
        return -1;
    }

    int current_segment = tail / SEGMENT_SIZE;
    while(data->segsums[current_segment].live_bytes > 0) {
        current_segment = current_segment % (data->segment_count - 1) + 1;
    }

    return current_segment * SEGMENT_SIZE;
}

off_t increment_tail(off_t tail) {
    // threading
    struct lfs_data* data = PRIVATE_DATA;
    struct segsum_entry* entry;
    do {
        tail += BLOCK_SIZE;
        if(tail >= data->log_size) {
            tail = SEGMENT_SIZE;
        }
        entry = get_segsum_entry(tail);
    } while(entry->file_owner != 0);

    return tail;
}

struct segment_summary* get_segsum(off_t offset) {
    int segment = offset / SEGMENT_SIZE;

    return &(PRIVATE_DATA->segsums[segment]);
}

struct segsum_entry* get_segsum_entry(off_t offset) {
    int index = offset % SEGMENT_SIZE / BLOCK_SIZE;

    return &(get_segsum(offset)->entries[index]);
}

void clear_segsum_entries(off_t* offsets, int offset_count) {
    struct lfs_data* data = PRIVATE_DATA;
    struct segment_summary* segsum;
    struct segsum_entry* entry;
    for(int index = 0; index < offset_count; index++) {
        if(offsets[index] != -1) {
            segsum = get_segsum(offsets[index]);
            entry = get_segsum_entry(offsets[index]);
            entry->file_owner = 0;
            entry->file_offset = 0;
            segsum->live_bytes -= BLOCK_SIZE;
            if(segsum->live_bytes == 0) {
                data->clean_segments++;
            }
        }
    }
}
