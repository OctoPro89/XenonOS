#pragma once

#include <arch/x86_64/sync/sync.h>
#include <io/file.h>
#include <io/fd.h>
#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>

#define MAX_FDS 64

typedef struct {
    spinlock_t lock;
    file_t* files[MAX_FDS];
} fd_table_t;

void fd_table_init(fd_table_t* table);
void fd_table_destroy(fd_table_t* table);

fd_t fd_table_alloc(fd_table_t* table, file_t* file);
file_t* fd_table_get(fd_table_t* table, fd_t fd);
int fd_table_close(fd_table_t* table, fd_t fd);
int fd_table_clone(fd_table_t* dst, fd_table_t* src);