#include "380LFS.h"
#include "link_ops.h"
#include "metadata_ops.h"
#include "metadata_helpers.h"

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*int lfs_link(const char *path, const char *newpath) {
    fprintf(stderr, "CALLED LINK\n");
    return -1;
}*/

int lfs_unlink(const char *path) {
    if(strcmp(path, "/") == 0) {
        fprintf(stderr, "unlink: cannot unlink root\n");

        return -1;
    }

    struct superblock sblock;
    if(get_superblock(&sblock) == NULL) {
        return -1;
    }

    
    struct inode_map root_imap;
    struct inode root;
    if(get_imap(ROOT_INUMBER, &sblock, &root_imap) == NULL
            || get_inode(ROOT_INUMBER, &sblock, &root) == NULL) {
        return -1;
    }

    int dir_read_size = BLOCK_SIZE * root.statbuf.st_blocks;
    struct dir_block* dblocks = (struct dir_block*) malloc(dir_read_size);
    if(dblocks == NULL) {
        fprintf(stderr, "unlink: malloc failed\n");

        return -1;
    }

    int read_result = read_blocks_all(&root, (char*) dblocks);
    if(read_result < dir_read_size) {
        if(read_result <= 0) {
            fprintf(stderr, "failed to read block %d of root\n", -read_result);
        } else {
            fprintf(stderr, "failed to read root\n");
        }
        free(dblocks);

        return -1;
    }
    int entry_count = root.statbuf.st_size / sizeof(struct dir_entry);
    int entry = 0;
    struct dir_entry* entry_ptr = &(dblocks[0].entries[0]);
    while(entry < entry_count && strcmp(entry_ptr->name, path) != 0) {
        entry++;
        entry_ptr++;
    }
    if(entry == entry_count) {
        fprintf(stderr, "unlink: file %s not found\n", path);
        free(dblocks);

        return -1;
    }

    struct inode file;
    if(get_inode(entry_ptr->inumber, &sblock, &file) == NULL) {
        free(dblocks);

        return -1;
    }
    int old_offset_count = 1 + file.statbuf.st_blocks;
    off_t* old_offsets = (off_t*) malloc(old_offset_count * sizeof(off_t));
    if(old_offsets == NULL) {
        fprintf(stderr, "unlink: malloc failed\n");
        free(dblocks);

        return -1;
    }

    old_offsets[0] = file.offset;
    for(int block = 0; block < file.statbuf.st_blocks; block++) {
        old_offsets[block + 1] = file.direct_blocks[block];
    }

    if(lfs_truncate(path, 0) == -1) {
        free(dblocks);
        free(old_offsets);

        return -1;
    }
    if(lfs_truncate("/", 
                    root.statbuf.st_size - sizeof(struct dir_entry)) == -1) {
        return -1;
    }

    // replace unlinked entry with final entry
    int entries_per_block = BLOCK_SIZE / sizeof(struct dir_entry);
    struct dir_block* last_dblock = dblocks + root.statbuf.st_blocks - 1;
    int last_entry_index = (entry_count - 1) % entries_per_block;
    struct dir_entry* last_entry = &(last_dblock->entries[last_entry_index]);
    root.statbuf.st_size -= sizeof(struct dir_entry);
    root.statbuf.st_blocks = root.statbuf.st_size / BLOCK_SIZE + 1;

    // only need to write block with removed entry, inode, imap
    off_t removed_entry_offset = entry * sizeof(struct dir_entry);
    int write_result = lfs_write_helper(&sblock, &root_imap, &root,
                                        (char*) last_entry,
                                        sizeof(struct dir_entry),
                                        removed_entry_offset);
    free(dblocks);
    if(write_result < sizeof(struct dir_entry)) {
        free(old_offsets);

        return -1;
    }

    clear_segsum_entries(old_offsets, old_offset_count);
    free(old_offsets);
    PRIVATE_DATA->file_count--;

    return 0;
}
/*
int lfs_readlink(const char *path, char *link, size_t size) {
    fprintf(stderr, "CALLED READLINK\n");
    return -1;
}

int lfs_symlink(const char *path, const char *link) {
    fprintf(stderr, "CALLED SYMLINK\n");
    return -1;
}

int lfs_rename(const char *path, const char *newpath) {
    fprintf(stderr, "CALLED RENAME\n");
    return -1;
}*/
