#include <unistd.h>
#include <arch/x86_64/syscall.h>

ssize_t read(int fd, void *buf, size_t count) {
    long ret;

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(0),
          "D"(fd),
          "S"(buf),
          "d"(count)
        : "rcx", "r11", "r10", "memory"
    );

    return (ssize_t)ret;
}

ssize_t write(int fd, const void *buf, size_t count) {
    long ret;

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(1),
          "D"(fd),
          "S"(buf),
          "d"(count)
        : "rcx", "r11", "r10", "memory"
    );

    return (ssize_t)ret;
}

pid_t spawn(const char *path) {
    long ret;

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(4),
          "D"(path)
        : "rcx", "r11", "r10", "memory"
    );

    return (pid_t)ret;
}

int wait(pid_t pid) {
    long ret;

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(5),
          "D"(pid)
        : "rcx", "r11", "r10", "memory"
    );

    return (int)ret;
}