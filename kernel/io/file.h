/* io/file.h */
#pragma once

#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>

typedef struct file file_t;

typedef struct {
    ssize_t (*read)(file_t *, void *, size_t);
    ssize_t (*write)(file_t *, const void *, size_t);
    int     (*close)(file_t *);
} file_ops_t;

typedef struct file {
    const file_ops_t *ops;
    void *private;
    u32 flags;
    u32 refcount;
} file_t;