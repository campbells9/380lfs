#include "benchmarks.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#define FILE_COUNT 10000
#define FILESIZE KB

#define MAX_FILENAME 10
#define FILENAME_FMT "file%4x"

void phase1() {
    int random_data = open("/dev/urandom", O_RDONLY);
    if(random_data < 0) {
        fprintf(stderr, "Failed to open /dev/urandom\n");

        exit(-1);
    }

    char name[MAX_FILENAME];
    char* fmt = FILENAME_FMT;
    int fd;
    char buffer[FILESIZE];
    //start timer
    struct timespec start, end;
    clock_gettime(CLOCK_REALTIME, &start);
    for(int i = 0; i < FILE_COUNT; i++) {
        snprintf(name, MAX_FILENAME, fmt, i);
        fd = open(name, O_CREAT | O_WRONLY, 0644);
        read(random_data, buffer, FILESIZE);
        write(fd, buffer, FILESIZE);
        close(fd);
    }
    //stop timer
    clock_gettime(CLOCK_REALTIME, &end);
    close(random_data);
    //print results
    print_results(&start, &end);
}

void phase2() {
    char name[MAX_FILENAME];
    char* fmt = FILENAME_FMT;
    int fd;
    char buffer[FILESIZE];
    //start timer
    struct timespec start, end;
    clock_gettime(CLOCK_REALTIME, &start);
    for(int i = 0; i < FILE_COUNT; i++) {
        snprintf(name, MAX_FILENAME, fmt, i);
        fd = open(name, O_RDONLY);
        read(fd, buffer, FILESIZE);
        close(fd);
    }
    //stop timer
    clock_gettime(CLOCK_REALTIME, &end);
    //print results
    print_results(&start, &end);
}

void phase3() {
    char name[MAX_FILENAME];
    char* fmt = FILENAME_FMT;
    int fd;
    //start timer
    struct timespec start, end;
    clock_gettime(CLOCK_REALTIME, &start);
    for(int i = 0; i < FILE_COUNT; i++) {
        snprintf(name, MAX_FILENAME, fmt, i);
        unlink(name);
    }
    //stop timer
    clock_gettime(CLOCK_REALTIME, &end);
    //print results
    print_results(&start, &end);
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
    printf("%-30s: %f\n", "files/sec", FILE_COUNT / elapsed_time);
}

int main() {
    printf("Small File Benchmark\n");
    printf("\nPhase 1: File Creation Phase\n");
    phase1();
    clear_l1_cache();
    printf("\nPhase 2: Read Phase\n");
    phase2();
    clear_l1_cache();
    printf("\nPhase 3: File Deletion Phase\n");
    phase3();

    return 0;
}
