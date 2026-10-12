#pragma once

#include <io/file.h>
#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>

typedef int fd_t;

#define FD_INVALID ((fd_t)-1)

/**
 * @brief Returns a referenced file.
 *
 * @note The caller owns the returned reference and must call file_put() when finished.
 */
file_t* fd_get(fd_t fd);

fd_t fd_alloc(file_t* file);
int fd_close(fd_t fd);