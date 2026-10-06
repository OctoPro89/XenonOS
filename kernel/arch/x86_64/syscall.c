#include "syscall.h"
#include <arch/x86_64/io.h>
#include <io/fd_table.h>
#include <io/fd.h>
#include <errno.h>
#include <process.h>
#include <memory/user.h>

// TODO: not sure if chunking is the best

u64 user_rsp_save;

#define SYSCALL_IO_BUFFER_SIZE 512

ssize_t sys_read(fd_t fd, void* user_buffer, size_t size) {
    u8 buffer[SYSCALL_IO_BUFFER_SIZE];
    size_t total = 0;

    if (!user_buffer && size != 0) {
        return -EFAULT;
    }

    file_t* file = fd_get(fd);

    if (!file) {
        return -EBADF;
    }

    if (!file->ops || !file->ops->read) {
        file_put(file);
        return -EINVAL;
    }

    while (total < size) {
        size_t chunk = size - total;
        if (chunk > sizeof(buffer)) {
            chunk = sizeof(buffer);
        }

        ssize_t result = file->ops->read(file, buffer, chunk);

        if (result < 0) {
            if (total != 0) {
                break;
            }

            file_put(file);
            return result;
        }

        if (result == 0) {
            break;
        }

        if (copy_to_user((u8*)user_buffer + total, buffer, (size_t)result) > 0) {
            file_put(file);
            if (total != 0) {
                return (ssize_t)total;
            }

            return -EFAULT;
        }

        total += (size_t)result;
        
        if ((size_t)result < chunk) {
            break;
        }
    }

    file_put(file);

    return (ssize_t)total;
}

ssize_t sys_write(fd_t fd, const void* user_buffer, size_t size) {
    u8 buffer[SYSCALL_IO_BUFFER_SIZE];
    size_t total = 0;

    if (!user_buffer && size != 0) {
        return -EFAULT;
    }

    file_t* file = fd_get(fd);

    if (!file) {
        return -EBADF;
    }

    if (!file->ops || !file->ops->write) {
        file_put(file);
        return -EINVAL;
    }

    while (total < size) {
        size_t chunk = size - total;
        if (chunk > sizeof(buffer)) {
            chunk = sizeof(buffer);
        }

        if (copy_from_user(buffer, (const u8*)user_buffer + total, chunk) < 0) {
            file_put(file);
            if (total != 0) {
                return (ssize_t)total;
            }

            return -EFAULT;
        }

        ssize_t result = file->ops->write(file, buffer, chunk);

        if (result < 0) {
            if (total != 0) {
                break;
            }
            
            file_put(file);
            return result;
        }

        if (result == 0) {
            break;
        }

        total += (size_t)result;

        if ((size_t)result < chunk) {
            break;
        }
    }

    file_put(file);

    return (ssize_t)total;
}

int sys_close(fd_t fd) {
    return fd_close(fd);
}

__attribute__((noreturn)) void sys_exit(int code) {
    process_t* process = process_current();
    if (process) {
        process_exit(process, code);
    }

    task_exit();

    __builtin_unreachable();
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
        case SYS_EXIT: {
            sys_exit((int)r->rdi);
        }
        default: {
            return (u64)-ENOSYS;
        }
    }
}

void syscall_handler(struct syscall_regs* r) {
    r->rax = syscall_dispatch(r);
}