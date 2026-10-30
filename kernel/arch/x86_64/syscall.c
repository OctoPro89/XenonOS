#include "syscall.h"
#include <arch/x86_64/io.h>
#include <arch/x86_64/msr.h>
#include <arch/x86_64/cpuid.h>
#include <arch/x86_64/uapi_cpuid.h>
#include <io/fd_table.h>
#include <io/fd.h>
#include <errno.h>
#include <process.h>
#include <elf/elf.h>
#include <windowing/window/window.h>
#include <windowing/window/internal.h>
#include <windowing/compositor/compositor.h>
#include <xlibc/string.h>
#include <xlibc/stdio.h>
#include <fcntl.h>
#include <tty/terminal.h>
#include <filesystem/vfs/vfs.h>
#include <memory/user.h>
#include <memory/heap.h>
#include <memory/paging.h>
#include <time/time.h>
#include <drivers/hwmon/amd_smn.h>
#include <drivers/hwmon/amd_zen_temp.h>
#include <smp/smp.h>

#define REG_EAX 0
#define REG_EBX 1
#define REG_ECX 2
#define REG_EDX 3

// TODO: not sure if chunking is the best

u64 user_rsp_save;

#define SYSCALL_IO_BUFFER_SIZE 512

#define SPAWN_MAX_ARGS       32
#define SPAWN_MAX_ARG_BYTES  1024

/*
 * Copy argv from userspace into kernel-owned storage.
 *
 * storage must point to SPAWN_MAX_ARG_BYTES bytes.
 * kargv must have room for SPAWN_MAX_ARGS pointers.
 *
 * The copied strings are packed into storage, and kargv
 * points into that storage.
 */
static int copy_spawn_arguments(
    int argc,
    const char* const* user_argv,
    char* storage,
    const char** kargv
) {
    if (argc < 0 || argc > SPAWN_MAX_ARGS) {
        return -EINVAL;
    }

    if (argc > 0 && !user_argv) {
        return -EFAULT;
    }

    size_t used = 0;

    for (int i = 0; i < argc; ++i) {
        const char* user_arg = NULL;

        if (copy_from_user(
                &user_arg,
                &user_argv[i],
                sizeof(user_arg)
            ) < 0) {
            return -EFAULT;
        }

        if (!user_arg) {
            return -EFAULT;
        }

        if (used >= SPAWN_MAX_ARG_BYTES) {
            return -E2BIG;
        }

        kargv[i] = storage + used;

        size_t remaining = SPAWN_MAX_ARG_BYTES - used;
        size_t copied = 0;

        while (copied < remaining) {
            char c;

            if (copy_from_user(&c, user_arg + copied, 1) < 0) {
                return -EFAULT;
            }

            storage[used + copied] = c;
            ++copied;

            if (c == '\0') {
                break;
            }
        }

        if (copied == remaining &&
            storage[used + copied - 1] != '\0') {
            return -E2BIG;
        }

        used += copied;
    }

    return 0;
}

static ssize_t directory_read(file_t* file, void* buffer, size_t size) {
    (void)file;
    (void)buffer;
    (void)size;
    return -EISDIR;
}

static ssize_t directory_write(file_t* file, const void* buffer, size_t size) {
    (void)file;
    (void)buffer;
    (void)size;
    return -EISDIR;
}

static ssize_t directory_seek(file_t* file, ssize_t offset, int whence) {
    (void)file;
    (void)offset;
    (void)whence;
    return -EISDIR;
}

static int directory_close(file_t* file) {
    if (!file) { return -EINVAL; }

    VFS_DIR* dir = (VFS_DIR*)file->private;

    file->private = NULL;

    if (dir) {
        vfs_closedir(dir);
    }

    return 0;
}

static const file_ops_t directory_file_ops = {
    .read  = directory_read,
    .write = directory_write,
    .seek  = directory_seek,
    .close = directory_close,
};

/**
 * @brief Copy a bounded, NULL-terminated path from userspace.
 * @returns 0 on success or a negative errno value
 */
static int copy_user_path(char out[USER_PATH_MAX], const char* user_path) {
    if (!user_path) {
        return -EFAULT;
    }

    for (size_t i = 0; i < USER_PATH_MAX; ++i) {
        if (copy_from_user(&out[i], user_path + i, 1) < 0) {
            return -EFAULT;
        }

        if (out[i] == '\0') {
            return 0;
        }
    }

    out[USER_PATH_MAX - 1] = '\0';
    return -ENAMETOOLONG;
}

static ssize_t regular_read(file_t* file, void* buffer, size_t size) {
    VFS_FILE* vf = (VFS_FILE*)file->private;

    if (!vf) { return -EBADF; }

    return (ssize_t)vfs_read(vf, buffer, size);
}

static ssize_t regular_write(file_t* file, const void* buffer, size_t size) {
    VFS_FILE* vf = (VFS_FILE*)file->private;

    if (!vf) { return -EBADF; }

    return (ssize_t)vfs_write(vf, buffer, size);
}

static ssize_t regular_seek(file_t* file, ssize_t offset, int whence) {
    VFS_FILE* vf = (VFS_FILE*)file->private;

    if (!vf) { return -EBADF; }

    ssize_t base;

    switch (whence) {
        case SEEK_SET:
            base = 0;
            break;

        case SEEK_CUR:
            base = (ssize_t)vfs_tell(vf);
            break;

        case SEEK_END:
            base = (ssize_t)vfs_get_size(vf);
            break;

        default:
            return -EINVAL;
    }

    ssize_t target;

    if (__builtin_add_overflow(base, offset, &target)) {
        return -EINVAL;
    }

    if (target < 0) return -EINVAL;

    int result = vfs_seek(vf, (u32)target);

    if (result < 0) return result;

    return target;
}

static int regular_close(file_t* file) {
    if (!file) return -EINVAL;

    VFS_FILE* vf = (VFS_FILE*)file->private;
    file->private = NULL;

    if (vf) {
        vfs_close(vf);
    }

    return 0;
}

static const file_ops_t regular_file_ops = {
    .read  = regular_read,
    .write = regular_write,
    .seek  = regular_seek,
    .close = regular_close,
};

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

    if ((file->open_flags & O_ACCMODE) == O_WRONLY) {
        file_put(file);
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

        if (copy_to_user((u8*)user_buffer + total, buffer, (size_t)result) < 0) {
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

    if ((file->open_flags & O_ACCMODE) == O_RDONLY) {
        file_put(file);
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

u64 sys_spawn(const char* user_path, int argc, const char* const* user_argv) {
    if (!user_path) {
        return (u64)(i64)-EFAULT;
    }

    if (argc < 0 || argc > SPAWN_MAX_ARGS) {
        return (u64)(i64)-EINVAL;
    }

    char* arg_storage = kmalloc(SPAWN_MAX_ARG_BYTES);
    if (!arg_storage) {
        return (u64)(i64)-ENOMEM;
    }

    const char* kargv[SPAWN_MAX_ARGS];

    int result = copy_spawn_arguments(
        argc,
        user_argv,
        arg_storage,
        kargv
    );

    if (result < 0) {
        kfree(arg_storage);
        return (u64)(i64)result;
    }

    char path[USER_PATH_MAX];

    result = copy_user_path(path, user_path);
    if (result < 0) {
        kfree(arg_storage);
        return (u64)(i64)result;
    }

    process_t* parent = process_current();
    if (!parent) {
        kfree(arg_storage);
        return (u64)(i64)-ESRCH;
    }

    char resolved_path[USER_PATH_MAX];
    result = vfs_resolve_path(
        parent->cwd,
        path,
        resolved_path,
        sizeof(resolved_path)
    );
    if (result < 0) {
        kfree(arg_storage);
        return (u64)(i64)result;
    }

    process_t* process = process_create();
    if (!process) {
        kfree(arg_storage);
        return (u64)(i64)-ENOMEM;
    }

        process->parent = parent;

    strncpy(process->cwd, parent->cwd, sizeof(process->cwd) - 1);
    process->cwd[sizeof(process->cwd) - 1] = '\0';

    result = fd_table_clone(&process->fd_table, &parent->fd_table);
    if (result < 0) {
        process_destroy(process);
        kfree(arg_storage);
        return (u64)(i64)result;
    }

    result = process_load_elf(process, resolved_path);
    if (result < 0) {
        process_destroy(process);
        kfree(arg_storage);
        return (u64)(i64)result;
    }

    // build the child's initial stack only after its ELF and stack pages have been loaded and mapped
    result = process_setup_arguments(process, argc, kargv);
    if (result < 0) {
        process_destroy(process);
        kfree(arg_storage);
        return (u64)(i64)result;
    }

    // the child now owns copies of all argument strings, it no longer depends on arg_storage
    kfree(arg_storage);
    arg_storage = NULL;

    result = process_start(process);
    if (result < 0) {
        process_destroy(process);
        return (u64)(i64)result;
    }

    return (u64)process->pid;
}

u64 sys_wait(u64 pid) {
    process_t* parent = process_current();
    if (!parent) {
        return (u64)(i64)-ESRCH;
    }

    process_t* child = process_find(pid);
    if (!child) {
        return (u64)(i64)-ESRCH;
    }

    if (child->parent != parent) {
        return (u64)(i64)-ECHILD;
    }

    // IMPORTANT NOTE: process_wait() doesn't destroy anything, it waits for the zombie, then process_reap() destroys the task / address space
    int status = process_wait(child);
    process_reap(child);

    return (u64)(i64)status;
}

ssize_t sys_window_create(const window_config_t* user_config) {
    if (!user_config) {
        return -EINVAL;
    }

    process_t* crnt = process_current();

    if (!crnt) {
        return -ESRCH;
    }

    if (crnt->window) {
        return -EFAULT;
    }

    window_config_t config;

    if (copy_from_user(&config, user_config, sizeof(config)) < 0) {
        return -EFAULT;
    }

    if (config.rect.width == 0 || config.rect.height == 0) {
        return -EINVAL;
    }

    char title[WINDOW_TITLE_MAX];
    memset(title, 0, WINDOW_TITLE_MAX);

    if (config.title) {
        u32 i = 0;
        b8 terminated = false;

        for (i = 0; i < WINDOW_TITLE_MAX - 1; ++i) {
            if (copy_from_user(&title[i], config.title + i, 1) < 0) {
                return -EFAULT;
            }

            if (title[i] == '\0') {
                terminated = true;
                break;
            }
        }

        if (!terminated) {
            return -ENAMETOOLONG;
        }
    }

    // TODO: may need a lock on this not sure...
    config.title = (const char*)title; // DO NOT pass user string to kernel
    crnt->window = window_server_create_window(&config);
    if (!crnt->window) {
        return -ENOMEM;
    }

    return 0;
}

ssize_t sys_window_destroy() {
    // TODO: may need a lock on this not sure...
    process_t* crnt = process_current();

    if (!crnt) {
        return -ESRCH;
    }

    if (!crnt->window) {
        return -EFAULT;
    }

    compositor_release_surface(crnt->window->surface->handle);
    window_server_destroy_window(crnt->window);

    return 0;
}

ssize_t sys_window_damage(const window_rect_t* rect) {
    process_t* crnt = process_current();

    if (!crnt) {
        return -ESRCH;
    }

    if (!crnt->window) {
        return -EFAULT;
    }

    window_rect_t krect;
    copy_from_user(&krect, rect, sizeof(window_rect_t));

    window_server_damage(crnt->window, krect.x, krect.y, krect.width, krect.height);

    return 0;
}

ssize_t sys_window_memcpy_frame(const void* user_buffer, size_t size) {
    process_t* crnt = process_current();

    if (!crnt) {
        return -ESRCH;
    }

    if (!crnt->window) {
        return -EFAULT;
    }

    if (!user_buffer || size != (crnt->window->surface->width * crnt->window->surface->height * sizeof(u32))) {
        return -EINVAL;
    }

    return copy_from_user(crnt->window->surface->pixels, user_buffer, size);
}

ssize_t sys_window_pop_event(window_event_t* evt) {
    process_t* crnt = process_current();

    if (!crnt) {
        return -ESRCH;
    }

    if (!crnt->window || !crnt->window->events) {
        return -EFAULT;
    }

    window_event_t kevt;
    if (ring_buffer_pop(crnt->window->events, &kevt)) {
        return copy_to_user(evt, &kevt, sizeof(window_event_t));
    }

    return -EFAULT; // idk what to return on failure
}

ssize_t sys_window_setup_terminal_io() {
    // TODO: remove
    // TODO: may need a lock on this not sure...
    process_t* crnt = process_current();

    if (!crnt) {
        return -ESRCH;
    }

    if (!crnt->window) {
        return -EFAULT;
    }
    
    terminal_t* terminal = console_terminal();

    terminal_init(terminal);
    terminal_attach_window(terminal, crnt->window);
    terminal_start_input_task(terminal); // todo: stop task
    return process_setup_stdio(crnt, terminal);
}

ssize_t sys_window_subscribe_keyboard() {
    process_t* crnt = process_current();

    if (!crnt) {
        return -ESRCH;
    }

    if (!crnt->window) {
        return -EFAULT;
    }
    
    window_server_subscribe_to_keyboard(crnt->window);

    return 0;
}

ssize_t sys_opendir(const char* user_path) {
    char path[USER_PATH_MAX];
    char resolved[USER_PATH_MAX];

    int result = copy_user_path(path, user_path);
    if (result < 0) {
        return result;
    }

    process_t* process = process_current();
    if (!process) {
        return -ESRCH;
    }

    result = vfs_resolve_path(process->cwd, path, resolved, USER_PATH_MAX);
    VFS_DIR* dir = vfs_opendir(resolved);
    if (!dir) {
        // TODO: future VFS error interface can distinguish ENOENT, ENOTDIR, and I/O errors instead of returning NULL
        return -ENOENT;
    }

    file_t* file = kmalloc(sizeof(file_t));
    if (!file) {
        vfs_closedir(dir);
        return -ENOMEM;
    }

    file_init(file, &directory_file_ops, dir);
    file->flags |= FILE_FLAG_DIRECTORY;

    fd_t fd = fd_alloc(file);

    // drop initial reference
    file_put(file);

    if (fd == FD_INVALID) {
        return -EMFILE;
    }

    return (ssize_t)fd;
}

ssize_t sys_readdir(fd_t fd, sys_dirent_t* user_entry) {
    if (!user_entry) {
        return -EFAULT;
    }

    file_t* file = fd_get(fd);
    if (!file) {
        return -EBADF;
    }

    if (!(file->flags & FILE_FLAG_DIRECTORY)) {
        file_put(file);
        return -ENOTDIR;
    }

    VFS_DIR* dir = (VFS_DIR*)file->private;
    if (!dir) {
        file_put(file);
        return -EBADF;
    }

    VFS_DIRENT vfs_entry;
    int result = vfs_readdir(dir, &vfs_entry);

    if (result == 0) {
        file_put(file);
        return 0;
    }

    if (result < 0) {
        file_put(file);
        return -EIO;
    }

    sys_dirent_t entry;
    memset(&entry, 0, sizeof(sys_dirent_t));
    memcpy(entry.name, vfs_entry.name, sizeof(entry.name));
    entry.name[SYS_DIRENT_NAME_MAX] = '\0';
    entry.size = vfs_entry.size;
    entry.type = (u32)vfs_entry.type;

    int copy_result = copy_to_user(user_entry, &entry, sizeof(sys_dirent_t));

    file_put(file);

    if (copy_result < 0) {
        return -EFAULT;
    }

    return 1;
}

// TODO: implement shrinking
// TODO: this version assumes only one thread can modify a process's heap at a time,
// this will need a dedicated per-process heap lock. however don't use spinlock for this idk wat to do
ssize_t sys_sbrk(ssize_t increment) {
    process_t* process = process_current();

    if (!process) {
        return -ESRCH;
    }

    // TODO:
    if (increment < 0) {
        return -EINVAL;
    }

    u64 old_break = process->heap_break;

    if (increment == 0) {
        return (ssize_t)old_break;
    }

    u64 new_break;

    if (__builtin_add_overflow(old_break, (u64)increment, &new_break)) {
        return -ENOMEM;
    }

    if (new_break > process->heap_limit) {
        return -ENOMEM;
    }

    u64 map_start;
    u64 map_end;

    if (__builtin_add_overflow(old_break, (u64)(PAGE_SIZE - 1), &map_start)) {
        return -ENOMEM;
    }

    map_start &= ~(PAGE_SIZE - 1);

    if (__builtin_add_overflow(new_break, (u64)(PAGE_SIZE - 1), &map_end)) {
        return -ENOMEM;
    }

    map_end &= ~(PAGE_SIZE - 1);

    u64 page_va;

    for (page_va = map_start; page_va < map_end; page_va += PAGE_SIZE) {

        if (vmm_virt_to_phys(process->space, page_va) != 0) {
            break;
        }

        paddr_t phys = pmm_alloc_page();
        if (!phys) {
            break;
        }

        memset((void*)phys_to_hhdm(phys), 0, PAGE_SIZE);

        int result = vmm_map(process->space, page_va, phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE);

        if (result < 0) {
            pmm_free_page(phys);
            break;
        }
    }

    if (page_va < map_end) {
        // roll back pages mapped by this request, heap_break remains unchanged
        for (u64 rollback = map_start; rollback < page_va; rollback += PAGE_SIZE) {
            paddr_t phys = vmm_virt_to_phys(process->space, rollback);

            if (phys) {
                vmm_unmap(process->space, rollback);
                pmm_free_page(phys & ~(PAGE_SIZE - 1));
            }
        }

        return -ENOMEM;
    }

    process->heap_break = new_break;

    return (ssize_t)old_break;
}

// DEBUG SBRK
/*
ssize_t sys_sbrk(ssize_t increment) {
    process_t* process = process_current();

    if (!process) {
        return -ESRCH;
    }

    printf("sbrk ENTER: old=%lx increment=%lx limit=%lx\n", (unsigned long)process->heap_break, (unsigned long)increment, (unsigned long)process->heap_limit);

    // TODO:
    if (increment < 0) {
        return -EINVAL;
    }

    u64 old_break = process->heap_break;

    if (increment == 0) {
        return (ssize_t)old_break;
    }

    u64 new_break;

    if (__builtin_add_overflow(old_break, (u64)increment, &new_break)) {
        printf("__builtin_add_overflow(old_break, (u64)increment, &new_break) | new_break: %x", new_break);
        return -ENOMEM;
    }

    if (new_break > process->heap_limit) {
        printf("new_break > process->heap_limit | new_break; %x", new_break);
        return -ENOMEM;
    }

    u64 map_start;
    u64 map_end;

    if (__builtin_add_overflow(old_break, (u64)(PAGE_SIZE - 1), &map_start)) {
        printf("__builtin_add_overflow(old_break, (u64)(PAGE_SIZE - 1), &map_start)");
        return -ENOMEM;
    }

    map_start &= ~(PAGE_SIZE - 1);

    if (__builtin_add_overflow(new_break, (u64)(PAGE_SIZE - 1), &map_end)) {
        printf("__builtin_add_overflow(new_break, (u64)(PAGE_SIZE - 1), &map_end)");
        return -ENOMEM;
    }

    map_end &= ~(PAGE_SIZE - 1);

    printf("sbrk: old=%p increment=%lld new=%p limit=%p\n", (void*)old_break, (long long)increment, (void*)new_break, (void*)process->heap_limit);

    printf("sbrk: map_start=%p map_end=%p pages=%llu\n", (void*)map_start, (void*)map_end, (unsigned long long)((map_end - map_start) / PAGE_SIZE));

    u64 page_va;

    for (page_va = map_start; page_va < map_end; page_va += PAGE_SIZE) {

        if (vmm_virt_to_phys(process->space, page_va) != 0) {
            printf("invalid vmm_virt_to_phys!\n");
            break;
        }

        paddr_t phys = pmm_alloc_page();
        if (!phys) {
            printf("sbrk: pmm_alloc_page failed at VA %p\n", (void*)page_va);
            break;
        }

        memset((void*)phys_to_hhdm(phys), 0, PAGE_SIZE);

        int result = vmm_map(process->space, page_va, phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE);

        if (result < 0) {
            printf("sbrk: vmm_map failed: %d at VA %p\n", result, (void*)page_va);
            pmm_free_page(phys);
            break;
        }
    }

    if (page_va < map_end) {
        // roll back pages mapped by this request, heap_break remains unchanged
        for (u64 rollback = map_start; rollback < page_va; rollback += PAGE_SIZE) {
            paddr_t phys = vmm_virt_to_phys(process->space, rollback);

            if (phys) {
                vmm_unmap(process->space, rollback);
                pmm_free_page(phys & ~(PAGE_SIZE - 1));
            }
        }

        printf("page_va < map_end\n");

        return -ENOMEM;
    }

    process->heap_break = new_break;

    printf("success\n");

    return (ssize_t)old_break;
}
*/

ssize_t sys_getcwd(char *user_buffer, size_t size) {
    if (!user_buffer) {
        return -EFAULT;
    }

    if (size == 0) {
        return -EINVAL;
    }

    process_t *process = process_current();
    if (!process) {
        return -ESRCH;
    }

    // ensure the kernel's cwd is terminated
    size_t len = strnlen(process->cwd, sizeof(process->cwd));

    if (len == sizeof(process->cwd)) {
        return -EINVAL;
    }

    size_t required = len + 1;

    if (size < required) {
        return -ERANGE;
    }

    if (copy_to_user(user_buffer, process->cwd, required) < 0) {
        return -EFAULT;
    }

    return (ssize_t)required;
}

ssize_t sys_chdir(const char *user_path) {
    char path[USER_PATH_MAX];
    char resolved[USER_PATH_MAX];

    int result = copy_user_path(path, user_path);
    if (result < 0) {
        return result;
    }

    process_t* process = process_current();
    if (!process) {
        return -ESRCH;
    }

    result = vfs_resolve_path(process->cwd, path, resolved, sizeof(resolved));

    if (result < 0) {
        return result;
    }

    VFS_DIR* dir = vfs_opendir(resolved);
    if (!dir) {
        return -ENOENT;
    }

    vfs_closedir(dir);

    size_t len = strlen(resolved);
    if (len >= sizeof(process->cwd)) {
        return -ENAMETOOLONG;
    }

    memcpy(process->cwd, resolved, len + 1);
    return 0;
}

u64 sys_get_time_ms() {
    return ktimer_get_unscheduled_ms();
    // return ktimer_get_system_time_in_milliseconds();
}

ssize_t sys_get_cpu_info(cpu_info_t* info) {
    if (!info) { return -EINVAL; }

    cpu_info_t k_info;
    memset(&k_info, 0, sizeof(cpu_info_t));
    u32 regs[4];
    memset(&regs, 0, 4 * sizeof(u32));

    cpuid_raw(CPUID_VENDOR_ID, 0, regs);
    memcpy(k_info.vendor, &regs[REG_EBX], 4);
    memcpy(k_info.vendor + 4, &regs[REG_EDX], 4);
    memcpy(k_info.vendor + 8, &regs[REG_ECX], 4);
    k_info.vendor[12] = '\0';

    cpuid_raw(CPUID_FEATURES, 0, regs);
    u32 eax = regs[REG_EAX];
    k_info.ecx_features = regs[REG_ECX];
    k_info.edx_features = regs[REG_EDX];

    k_info.stepping = eax & 0x0F;
    u32 base_model = (eax >> 4) & 0x0F;
    u32 base_family = (eax >> 8) & 0x0F;
    u32 ext_model = (eax >> 16) & 0x0F;
    u32 ext_family = (eax >> 20) & 0xFF;

    k_info.family = (base_family == 0x0F) ? (base_family + ext_family) : base_family;
    k_info.model = (base_family == 0x06 || base_family == 0x0F) ? (base_model + (ext_model << 4)) : base_model;

    cpuid_raw(0x80000000, 0, regs);
    if (regs[REG_EAX] >= CPUID_BRAND_STRING_3) {
        char* ptr = k_info.brand;
        for (u32 leaf = CPUID_BRAND_STRING_1; leaf <= CPUID_BRAND_STRING_3; leaf++) {
            cpuid_raw(leaf, 0, regs);
            memcpy(ptr, regs, 16);
            ptr += 16;
        }
        *ptr = '\0';
    } else {
        memcpy(k_info.brand, "Unknown Processor", 18);
    }

    cpuid_raw(0x00000007, 0, regs);
    k_info.ecx_leaf7 = regs[REG_ECX];

    return copy_to_user(info, &k_info, sizeof(cpu_info_t));
}

ssize_t sys_open(const char* user_path, int flags) {
    if (!user_path) {
        return -EINVAL;
    }

    int access_mode = flags & O_ACCMODE;

    if (access_mode != O_RDONLY &&
        access_mode != O_WRONLY &&
        access_mode != O_RDWR) {
        return -EINVAL;
    }

    char path[USER_PATH_MAX];
    char resolved[USER_PATH_MAX];

    int result = copy_user_path(path, user_path);
    if (result < 0) {
        return result;
    }

    // no truncating a read-only open
    if ((flags & O_TRUNC) && access_mode == O_RDONLY) {
        return -EINVAL;
    }

    process_t* process = process_current();
    if (!process) {
        return -ESRCH;
    }

    result = vfs_resolve_path(process->cwd, path, resolved, sizeof(resolved));

    if (result < 0) return result;

    VFS_FILE* vf = vfs_open(resolved);
    b8 existed = (vf != NULL);

    // O_CREAT | O_EXCL must fail if the file already exists.
    if (existed && (flags & O_CREAT) && (flags & O_EXCL)) {
        vfs_close(vf);
        return -EEXIST;
    }

    // if the file does not exist, create it only when O_CREAT is specified
    if (!vf) {
        if (!(flags & O_CREAT)) {
            return -ENOENT;
        }

        vf = vfs_create(resolved);

        if (!vf) {
            // TODO: distinguish error
            return -EIO;
        }
    }

    // O_TRUNC empties an existing file; a newly created file is already empty, so truncating it again is harmless
    if (flags & O_TRUNC) {
        if (vfs_truncate(vf, 0) != 0) {
            vfs_close(vf);
            return -EIO;
        }

        if (vfs_seek(vf, 0) != 0) {
            vfs_close(vf);
            return -EIO;
        }
    }
    
    file_t* file = kmalloc(sizeof(file_t));
    if (!file) {
        vfs_close(vf);
        return -ENOMEM;
    }

    file_init(file, &regular_file_ops, vf);

    // store the open flags on the open-file object
    file->open_flags = flags;

    fd_t fd = fd_alloc(file);

    // drop initial reference
    file_put(file);

    if (fd == FD_INVALID) {
        return -EMFILE;
    }

    return (ssize_t)fd;
}

ssize_t sys_seek(fd_t fd, ssize_t offset, int whence) {
    file_t* file = fd_get(fd);
    if (!file) return -EBADF;

    if (!file->ops || !file->ops->seek) {
        file_put(file);
        return -EINVAL;
    }

    ssize_t result = file->ops->seek(file, offset, whence);

    file_put(file);
    return result;
}

void sys_sleep_ns(u32 ns) {
    task_sleep_ns(ns);
}

ssize_t sys_set_desktop_env_var(desktop_env_var_t var, void* val, size_t length) {
    if (!val || length == 0) { return -EINVAL; }
    
    switch (var) {
        case DESKTOP_ENV_VAR_WALLPAPER_IMAGE_PATH: {
            if (length > USER_PATH_MAX) { return -ENAMETOOLONG; }
            char path[USER_PATH_MAX];
            copy_user_path(path, val);
            return compositor_set_wallpaper(path) == true ? 0 : -EIO;
            // compositor_queue_set_wallpaper(path);
            break;
        }
        default: {
            return -EINVAL;
        }
    }

    return 0;
}

i32 sys_hwmon_get_cpu_temp_mc() {
    if (amd_smn_initialized()) {
        i32 millidegrees_c = 0;
        if (!amd_zen_read_temperature(&millidegrees_c)) {
            return -EAGAIN;
        }

        return millidegrees_c;
    }
    return msr_read_cpu_temperature(); // TODO: return in millidegrees celcius
}

u64 sys_hwmon_get_online_cpus() {
    return smp_online_count();
}

u64 sys_hwmon_get_total_cpus() {
    return smp_cpu_count();
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
            return sys_spawn((const char*)r->rdi, (int)r->rsi, (const char* const*)r->rdx);
        }
        case SYS_WAIT: {
            return sys_wait(r->rdi);
        }
        case SYS_WINDOW_CREATE: {
            return (u64)sys_window_create((const window_config_t*)r->rdi);
        }
        case SYS_WINDOW_DESTROY: {
            return (u64)sys_window_destroy();
        }
        case SYS_WINDOW_DAMAGE: {
            return (u64)sys_window_damage((const window_rect_t*)r->rdi);
        }
        case SYS_WINDOW_MEMCPY_FRAME: {
            return (u64)sys_window_memcpy_frame((const void*)r->rdi, (size_t)r->rsi);
        }
        case SYS_WINDOW_POP_EVENT: {
            return (u64)sys_window_pop_event((window_event_t*)r->rdi);
        }
        case SYS_WINDOW_SETUP_TERMINAL_IO: {
            return (u64)sys_window_setup_terminal_io();
        }
        case SYS_WINDOW_SUBSCRIBE_KEYBOARD: {
            return sys_window_subscribe_keyboard();
        }
        case SYS_OPENDIR: {
            return (u64)sys_opendir((const char*)r->rdi);
        }
        case SYS_READDIR: {
            return (u64)sys_readdir((fd_t)r->rdi, (sys_dirent_t*)r->rsi);
        }
        case SYS_SBRK: {
            return (u64)sys_sbrk((ssize_t)r->rdi);
        }
        case SYS_CHDIR: {
            return (u64)sys_chdir((const char*)r->rdi);
        }
        case SYS_GETCWD: {
            return (u64)sys_getcwd((char*)r->rdi, (size_t)r->rsi);
        }
        case SYS_GET_TIME_MS: {
            return sys_get_time_ms();
        }
        case SYS_GET_CPU_INFO: {
            return (u64)sys_get_cpu_info((cpu_info_t*)r->rdi);
        }
        case SYS_OPEN: {
            return (u64)sys_open((const char*)r->rdi, (int)r->rsi);
        }
        case SYS_SEEK: {
            return (u64)sys_seek((fd_t)r->rdi, (ssize_t)r->rsi, (int)r->rdx);
        }
        case SYS_SLEEP_NS: {
            sys_sleep_ns((u32)r->rdi);
            return 0;
        }
        case SYS_SET_DESKTOP_ENV_VAR: {
            return (u64)sys_set_desktop_env_var((desktop_env_var_t)r->rdi, (void*)r->rsi, (size_t)r->rdx);
        }
        case SYS_HWMON_GET_CPU_TEMP_MC: {
            return (u64)(i64)sys_hwmon_get_cpu_temp_mc();
        }
        case SYS_HWMON_GET_ONLINE_CPUS: {
            return sys_hwmon_get_online_cpus();
        }
        case SYS_HWMON_GET_TOTAL_CPUS: {
            return sys_hwmon_get_total_cpus();
        }
        default: {
            return (u64)-ENOSYS;
        }
    }
}

void syscall_handler(struct syscall_regs* r) {
    r->rax = syscall_dispatch(r);
}