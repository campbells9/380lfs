# 380LFS

A log-structured file system implemented with the FUSE interface.

To make the FUSE client:

`make`

To make the benchmarks:

`make benchmarks`

To make both:

`make all`

To mount the client:

`./380LFS -s [FUSE options] [mountpoint] [log file] [size (GB)]`

Log file is created with the given size (GB) if it did not already exist.
If it already exists, [size] is ignored.

To remove all executables:

`make clean`

