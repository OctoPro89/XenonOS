#pragma once

#include <xlibc/xstdint.h>

#define SYS_DIRENT_NAME_MAX 255

typedef struct {
    char name[SYS_DIRENT_NAME_MAX + 1];
    u32 size;
    u32 type;
} sys_dirent_t;