#include "syscall.h"
#include <arch/x86_64/io.h>
#include <io/fd_table.h>
#include <io/fd.h>
#include <errno.h>

u64 user_rsp_save;

ssize_t sys_read(fd_t fd, void* buffer, size_t size) {
    file_t* file = fd_get(fd);

    if (!file) {
        return -EBADF;
    }

    if (!file->ops || !file->ops->read) {
        file_put(file);
        return -EINVAL;
    }

    // TODO: copy_from_user, copy_to_user, user_range_valid
    ssize_t result = file->ops->read(file, buffer, size);

    file_put(file);

    return result;
}

ssize_t sys_write(fd_t fd, const void* buffer, size_t size) {
    file_t* file = fd_get(fd);

    if (!file) {
        return -EBADF;
    }

    if (!file->ops || !file->ops->write) {
        file_put(file);
        return -EINVAL;
    }

    ssize_t result = file->ops->write(file, buffer, size);

    file_put(file);

    return result;
}

int sys_close(fd_t fd) {
    return fd_close(fd);
}

u64 syscall_dispatch(struct syscall_regs* r) {
    switch (r->rax) {
        case SYS_READ: {
            return (u64)sys_read((fd_t)r->rdi, (void*)r->rsi, (size_t)r->rdx);
        }
        case SYS_WRITE: {
            return (u64)sys_write((fd_t)r->rdi, (const void*)r->rsi, (size_t)r->rdx);
        }
        case SYS_CLOSE: {
            return (u64)sys_close((fd_t)r->rdi);
        }
        default: {
            return (u64)-ENOSYS;
        }
    }
}

void syscall_handler(struct syscall_regs* r) {
    r->rax = syscall_dispatch(r);
}