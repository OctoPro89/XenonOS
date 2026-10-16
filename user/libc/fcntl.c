#include <fcntl.h>
#include <arch/x86_64/syscall.h>

ssize_t open(const char* path, int flags) {
    long ret;

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_OPEN),
          "D"(path),
          "S"(flags)
        : "rcx", "r11", "r10", "memory"
    );

    return (ssize_t)ret;
}