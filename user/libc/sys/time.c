#include <sys/time.h>
#include <xlibc/time.h>

// for compatibility
int gettimeofday(struct timeval* tv, struct timezone* tz) {
    if (!tv) {
        // NOTE: POSIX doesn't strictly fail on null tv if tz is provided but it is invalid for most applications
        return 0;
    }

    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) < 0) {
        return -1; // errno filled by clock_gettime
    }

    // convert nanoseconds (10^-9) down to microseconds (10^-6)
    tv->tv_sec = ts.tv_sec;
    tv->tv_usec = ts.tv_nsec / 1000;

    // TODO: fill in
    if (tz) {
        tz->tz_minuteswest = 0;
        tz->tz_dsttime = 0;
    }

    return 0;
}