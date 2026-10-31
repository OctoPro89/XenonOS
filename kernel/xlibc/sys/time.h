#pragma once

struct timeval {
    long tv_sec; // seconds
    long tv_usec; // microseconds
};

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

int gettimeofday(struct timeval* tv, struct timezone* tz);