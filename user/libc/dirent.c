#include <xlibc/dirent.h>
#include <xlibc/string.h>
#include <xlibc/stdlib.h>
#include <arch/x86_64/syscall.h>
#include <errno.h>
#include <unistd.h>
#include <filesystem/sys_dirent.h>

long __sys_opendir(const char* path) {
    long ret;

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_OPENDIR),
          "D"(path)
        : "rcx", "r11", "r10", "memory"
    );

    return ret;
}

long __sys_readdir(int fd, sys_dirent_t* entry) {
    long ret;

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_READDIR),
          "D"(fd),
          "S"(entry)
        : "rcx", "r11", "r10", "memory"
    );

    return ret;
}

struct __dirstream {
    int fd;
    struct dirent entry;
};

DIR* opendir(const char* name) {
    if (!name) {
        errno = EINVAL;
        return NULL;
    }

    long result = __sys_opendir(name);
    if (result < 0) {
        errno = (int)-result;
        return NULL;
    }

    DIR* dir = malloc(sizeof(DIR));
    if (!dir) {
        close((int)result);
        errno = ENOMEM;
        return NULL;
    }

    memset(dir, 0, sizeof(*dir));
    dir->fd = (int)result;
    return dir;
}

struct dirent* readdir(DIR* dirp) {
    if (!dirp) {
        errno = EINVAL;
        return NULL;
    }

    sys_dirent_t raw;
    long result = __sys_readdir(dirp->fd, &raw);

    if (result == 0) {
        return NULL; // end of directory
    }

    if (result < 0) {
        errno = (int)-result;
        return NULL;
    }

    memset(&dirp->entry, 0, sizeof(dirp->entry));
    memcpy(dirp->entry.d_name, raw.name, NAME_MAX + 1);
    dirp->entry.d_name[NAME_MAX] = '\0';
    dirp->entry.d_ino = 0;
    dirp->entry.d_type = (unsigned char)raw.type;

    return &dirp->entry;
}

int closedir(DIR* dirp) {
    if (!dirp) {
        errno = EINVAL;
        return -1;
    }

    int result = close(dirp->fd);
    int saved_errno = errno;

    free(dirp);

    if (result < 0) {
        errno = saved_errno;
        return -1;
    }

    return 0;
}