#ifndef _SEGMENTS_H_
#define _SEGMENTS_H_

#include "380LFS.h"

// clean when number of clean segments falls below threshold
#define START_CLEAN_SEGMENT_THRESHOLD 20
// number of dirty segments to clean per iteration
#define SEGMENTS_PER_CLEAN 20
// stop cleaning once number of clean segments falls above threshold
#define STOP_CLEAN_SEGMENT_THRESHOLD 75

#define SEGSUM_ROOT (-1)
#define SEGSUM_METADATA (-2)
#define SEGSUM_DOUBLE_INDIRECT (-3)
#define SEGSUM_INDIRECT (-4)

#define ROUND_DOWN_SEGMENT(size) ((size) / SEGMENT_SIZE * SEGMENT_SIZE)
#define ROUND_UP_SEGMENT(size) ROUND_DOWN_SEGMENT((size) + SEGMENT_SIZE)

#define NSEC_PER_SEC 1000000000

void clean();
off_t find_next_clean_segment(off_t);
off_t increment_tail(off_t);
struct segment_summary* get_segsum(off_t);
struct segsum_entry* get_segsum_entry(off_t);
void clear_segsum_entries(off_t*, int);

#endif
