#include "syscall.h"
#include <arch/x86_64/io.h>
#include <io/fd_table.h>
#include <io/fd.h>
#include <errno.h>
#include <process.h>
#include <memory/user.h>
#include <elf/elf.h>

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

u64 sys_spawn(const char* user_path) {
    if (!user_path) {
        return -EFAULT;
    }

    char path[256];

    // TODO: check <0
    if (copy_from_user(path, user_path, sizeof(path)) < 0) {
        return -EFAULT;
    }

    path[sizeof(path) - 1] = 0;

    process_t* parent = process_current();
    if (!parent) {
        return -ESRCH;
    }

    process_t* process = process_create();
    if (!process) {
        return -ENOMEM;
    }

    process->parent = parent;
    
    int result = process_load_elf(process, path);

    if (result < 0) {
        process_destroy(process);
        return result;
    }

    result = process_start(process);
    if (result < 0) {
        process_destroy(process);
        return result;
    }

    return process->pid;
}

u64 sys_wait(u64 pid) {
    process_t* parent = process_current();
    if (!parent) {
        return -ESRCH;
    }

    process_t* child = process_find(pid);
    if (!child) {
        return -ESRCH;
    }

    if (child->parent != parent) {
        return -ECHILD;
    }

    // IMPORTANT NOTE: process_wait() doesn't destroy anything, it waits for the zombie, then process_reap() destroys the task / address space
    int status = process_wait(child);
    process_reap(child);

    return (u64)(i64)status;
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
            sys_exit((int)r->rdi); // TODO: not sure if this returns something or no
        }
        case SYS_SPAWN: {
            return sys_spawn((const char*)r->rdi);
        }
        case SYS_WAIT: {
            return sys_wait(r->rdi);
        }
        default: {
            return (u64)-ENOSYS;
        }
    }
}

void syscall_handler(struct syscall_regs* r) {
    r->rax = syscall_dispatch(r);
}