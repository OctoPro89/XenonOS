#include <time.h>
#include <errno.h>
#include <arch/x86_64/syscall.h>

// POSIX compliant time / clock spec

int clock_gettime(clockid_t clk_id, struct timespec* tp) {
    long ret;

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_CLOCK_GETTIME),
          "D"(clk_id),
          "S"(tp)
        : "rcx", "r11", "memory"
    );

    if (ret < 0) {
        errno = (int)(-ret);
        return -1;
    }

    return 0;
}

int clock_getres(clockid_t clk_id, struct timespec* res) {
    long ret;

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_CLOCK_GETRES),
          "D"(clk_id),
          "S"(res)
        : "rcx", "r11", "memory"
    );

    if (ret < 0) {
        errno = (int)(-ret);
        return -1;
    }

    return 0;
}

int nanosleep(const struct timespec* req, struct timespec* rem) {
    long ret;

    struct sys_nanosleep_args args = {
        .clk_id = CLOCK_MONOTONIC,
        .flags = 0,
        .user_req = req,
        .user_rem = rem
    };

    // maps directly onto clock_nanosleep using the MONOTONIC clock domain with relative flags (0) as per POSIX specification
    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_CLOCK_NANOSLEEP),
          "D"(&args)
        : "rcx", "r11", "memory"
    );

    if (ret < 0) {
        errno = (int)(-ret);
        return -1;
    }

    return 0;
}

int clock_nanosleep(clockid_t clk_id, int flags, const struct timespec* req, struct timespec* rem) {
    // TODO: four arguments in syscalls
    struct sys_nanosleep_args args = { clk_id, flags, req, rem };
    long ret;

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_CLOCK_NANOSLEEP),
          "D"(&args) // Passed cleanly via rdi (1st parameter)
        : "rcx", "r11", "memory"
    );

    // NOTE: POSIX clock_nanosleep returns the positive error number directly on failure instead of setting errno, return (-ret) to match this standard
    if (ret < 0) {
        return (int)(-ret);
    }

    return 0;
}