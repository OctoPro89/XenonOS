#pragma once

#include <io/file.h>
#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>

typedef int fd_t;

#define FD_INVALID ((fd_t)-1)

/*
 * These operate on the current process's FD table.
 */
fd_t    fd_alloc(file_t *file);
file_t *fd_get(fd_t fd);
void    fd_close(fd_t fd);

/*
 * Explicitly acquire/release a reference to a file.
 */
void file_get(file_t *file);
void file_put(file_t *file);