#ifndef _BENCHMARKS_H_
#define _BENCHMARKS_H_

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#define KB (1 << 10)
#define CACHE_SIZE 32 * KB

void print_results(struct timespec*, struct timespec*);

void clear_l1_cache() {
	int random_data = open("/dev/urandom", O_RDONLY);
    if(random_data < 0) {
    	perror("open error");
        fprintf(stderr, "Failed to open /dev/urandom\n");

        exit(-1);
    }

    int i, j;
    long* data = (long*) malloc(CACHE_SIZE * 2);
    if(data == NULL) {
    	fprintf(stderr, "Failed to clear cache, malloc failed\n");
    	close(random_data);

        exit(-1);
    }
    
    int len = CACHE_SIZE * 2 / sizeof(long);
    for(i = 0; i < 32; i++) {
        for(j = 0; j < len; j++) {
        	read(random_data, data + j, sizeof(long));
        }
    }
    close(random_data);
    free(data);
}

#endif
