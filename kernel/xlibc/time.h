#pragma once

#include <xlibc/xstddef.h>

typedef long time_t;
typedef long sutime_t;

struct timespec {
    time_t tv_sec; // seconds
    long tv_nsec; // nanoseconds (0 to 999,999,999)
};

typedef int clockid_t;

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_MONOTONIC_RAW 4

#define TIMER_ABSTIME 1

int clock_gettime(clockid_t clk_id, struct timespec* tp);
int clock_getres(clockid_t clk_id, struct timespec* res);
int nanosleep(const struct timespec* req, struct timespec* rem);