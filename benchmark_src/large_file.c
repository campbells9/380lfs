#include "benchmarks.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#define FILESIZE (100 * (KB * KB))
#define BLOCKSIZE (4 * KB)
#define BLOCKS_PER_FILE FILESIZE / BLOCKSIZE
#define FILENAME "large_file"

off_t* randomize_offsets(off_t[BLOCKS_PER_FILE]);

void seq_write_phase() {
    int random_data = open("/dev/urandom", O_RDONLY);
    if(random_data < 0) {
        fprintf(stderr, "Failed to open /dev/urandom\n");

        exit(-1);
    }

    char block[BLOCKSIZE];
    int bytes_written = 0;
    //start timer
    struct timespec start, end;
    clock_gettime(CLOCK_REALTIME, &start);
    int fd = open(FILENAME, O_CREAT | O_WRONLY, 0644);
    while(bytes_written < FILESIZE) {
        read(random_data, block, BLOCKSIZE);
        bytes_written += write(fd, block, BLOCKSIZE);
    }
    //stop timer
    clock_gettime(CLOCK_REALTIME, &end);
    close(random_data);
    ftruncate(fd, FILESIZE);
    close(fd);
    //print results
    print_results(&start, &end);
}

void seq_read_phase() {
    char block[BLOCKSIZE];
    int fd = open(FILENAME, O_RDONLY, 0644);
    //start timer
    struct timespec start, end;
    clock_gettime(CLOCK_REALTIME, &start);
    for(int i = 0; i < FILESIZE; i += BLOCKSIZE) {
        read(fd, block, BLOCKSIZE);
    }
    //stop timer
    clock_gettime(CLOCK_REALTIME, &end);
    close(fd);
    //print results
    print_results(&start, &end);
}

void rand_write_phase() {
    off_t offsets[BLOCKS_PER_FILE];
    int random_data = open("/dev/urandom", O_RDONLY);
    if(random_data < 0 || randomize_offsets(offsets) == NULL) {
        fprintf(stderr, "Failed to open /dev/urandom\n");

        exit(-1);
    }

    char block[BLOCKSIZE];
    int fd = open(FILENAME, O_WRONLY);
    //start timer
    struct timespec start, end;
    clock_gettime(CLOCK_REALTIME, &start);
    for(int i = 0; i < BLOCKS_PER_FILE; i++) {
        read(random_data, block, BLOCKSIZE);
        lseek(fd, offsets[i], SEEK_SET);
        write(fd, block, BLOCKSIZE);
    }
    //stop timer
    clock_gettime(CLOCK_REALTIME, &end);
    close(random_data);
    close(fd);
    //print results
    print_results(&start, &end);
}

void rand_read_phase() {
    off_t offsets[BLOCKS_PER_FILE];
    if(randomize_offsets(offsets) == NULL) {
        fprintf(stderr, "Failed to open /dev/urandom\n");

        exit(-1);
    }

    char block[BLOCKSIZE];
    int fd = open(FILENAME, O_RDONLY);
    //start timer
    struct timespec start, end;
    clock_gettime(CLOCK_REALTIME, &start);
    for(int i = 0; i < BLOCKS_PER_FILE; i++) {
        lseek(fd, offsets[i], SEEK_SET);
        read(fd, block, BLOCKSIZE);
    }
    //stop timer
    clock_gettime(CLOCK_REALTIME, &end);
    close(fd);
    //print results
    print_results(&start, &end);
}

off_t* randomize_offsets(off_t offsets[BLOCKS_PER_FILE]) {
    int i;
    for(i = 0; i < BLOCKS_PER_FILE; i++) {
        offsets[i] = (off_t) i * BLOCKSIZE;
    }

    int random_data = open("/dev/urandom", O_RDONLY);
    if(random_data < 0) {
        return NULL;
    }

    unsigned random;
    off_t temp;
    for(i--; i > 0; i--) {
        read(random_data, (char*) &random, sizeof(unsigned));
        random %= (unsigned) i;
        temp = offsets[random];
        offsets[random] = offsets[i];
        offsets[i] = temp;
    }
    close(random_data);

    return offsets;
}

void print_results(struct timespec* start, struct timespec* end) {
    time_t elapsed_sec = end->tv_sec - start->tv_sec;
    long elapsed_nsec = end->tv_nsec - start->tv_nsec;
    if(elapsed_nsec < 0L) {
        elapsed_nsec += 1000000000L;
        elapsed_sec--;
    }

    char* fmt = "%-30s: %ld.%06lds\n";
    printf(fmt, "Elapsed time", elapsed_sec, elapsed_nsec);

    double elapsed_time = elapsed_sec + (double) elapsed_nsec / 1000000000L;
    printf("%-30s: %f\n", "KB/sec", FILESIZE / KB / elapsed_time);
}

int main() {
    printf("Large File Benchmark\n");
    printf("\nPhase 1: Sequential Write Phase\n");
    seq_write_phase();
    clear_l1_cache();
    printf("\nPhase 2: Sequential Read Phase\n");
    seq_read_phase();
    clear_l1_cache();
    printf("\nPhase 3: Random Write Phase\n");
    rand_write_phase();
    clear_l1_cache();
    printf("\nPhase 4: Random Read Phase\n");
    rand_read_phase();
    clear_l1_cache();
    printf("\nPhase 5: Sequential Reread Phase\n");
    seq_read_phase();

    return 0;
}
