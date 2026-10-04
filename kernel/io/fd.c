#include <io/fd.h>
#include <io/file.h>
#include <io/fd_table.h>

#include <xlibc/xstddef.h>
#include <xlibc/xstdint.h>
#include <xlibc/stdlib.h>

#include <process.h>
#include <errno.h>

fd_t fd_alloc(file_t* file) {
    process_t* process;
    process = process_current();

    if (!process) {
        return FD_INVALID;
    }

    return fd_table_alloc(&process->fd_table, file);
}

file_t* fd_get(fd_t fd) {
    process_t* process = process_current();

    if (!process) {
        return NULL;
    }

    return fd_table_get(&process->fd_table, fd);
}

int fd_close(fd_t fd) {
    process_t* process = process_current();

    if (!process) {
        return -EINVAL;
    }

    return fd_table_close(&process->fd_table, fd);
}