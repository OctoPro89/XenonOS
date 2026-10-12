#pragma once

#include <xlibc/xstddef.h>
#include <xlibc/xstdint.h>

#define NAME_MAX 255

// POSIX directory stream, implementation is private to libc
typedef struct __dirstream DIR;

// POSIX-required fields, plus d_type as a common extension
struct dirent {
    u64 d_ino;
    char d_name[NAME_MAX + 1];
    unsigned char d_type;
};

#ifndef DT_UNKNOWN
#define DT_UNKNOWN 0
#endif

#ifndef DT_REG
#define DT_REG     1
#endif

#ifndef DT_DIR
#define DT_DIR     2
#endif

DIR* opendir(const char* name);
struct dirent* readdir(DIR* dirp);
int closedir(DIR* dirp);