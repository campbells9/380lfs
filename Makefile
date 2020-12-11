.PHONY: default, benchmarks, all, clean

CC = gcc
CFLAGS = $(shell pkg-config fuse --cflags --libs)
SOURCES = 380LFS.c metadata_helpers.c file_io_ops.c dir_ops.c metadata_ops.c fs_ops.c link_ops.c segments.c
OUTPUT = 380LFS

default: src
	cd src && $(CC) $(SOURCES) $(CFLAGS) -o ../$(OUTPUT)

benchmarks: benchmark_src
	cd benchmark_src && $(CC) small_file.c -o ../small_file_benchmark
	cd benchmark_src && $(CC) large_file.c -o ../large_file_benchmark

all: default benchmarks

clean:
	rm -f $(OUTPUT) small_file_benchmark large_file_benchmark
