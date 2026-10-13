#include <unistd.h>
#include <arch/x86_64/syscall.h>

ssize_t read(int fd, void *buf, size_t count) {
    long ret;

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_READ),
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
        : "a"(SYS_WRITE),
          "D"(fd),
          "S"(buf),
          "d"(count)
        : "rcx", "r11", "r10", "memory"
    );

    return (ssize_t)ret;
}

ssize_t close(int fd) {
    long ret;

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_CLOSE),
          "D"(fd)
        : "rcx", "r11", "r10", "memory"
    );

    return (ssize_t)ret;
}

void* sbrk(ssize_t increment) {
    long ret;

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_SBRK),
          "D"(increment)
        : "rcx", "r11", "r10", "memory"
    );

    if (ret < 0) {
        // errno = (int)ret;
        return (void*)-1;
    }

    return (void*)ret;
}

int chdir(const char* path) {
    long ret;

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_CHDIR),
          "D"(path)
        : "rcx", "r11", "r10", "memory"
    );

    // POSIX compatiblity
    if (ret < 0) {
        // errno = (int)ret;
        return -1;
    }

    return 0;
}

char* getcwd(char* buf, size_t size) {
    long ret;

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_GETCWD),
          "D"(buf),
          "S"(size)
        : "rcx", "r11", "r10", "memory"
    );

    // POSIX compatiblity
    if (ret < 0) {
        // errno = (int)ret;
        return (char*)-1;
    }

    return buf;
}

pid_t spawn(const char *path) {
    long ret;

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_SPAWN),
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
        : "a"(SYS_WAIT),
          "D"(pid)
        : "rcx", "r11", "r10", "memory"
    );

    return (int)ret;
}