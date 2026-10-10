#include <io/fd_table.h>
#include <errno.h>
#include <xlibc/string.h>

void fd_table_init(fd_table_t* table) {
    size_t i;

    spin_lock_init(&table->lock);

    for (i = 0; i < MAX_FDS; i++) {
        table->files[i] = NULL;
    }
}

void fd_table_destroy(fd_table_t* table) {
    size_t i;

    // this should only be called once no threads can still use the process's FD table
    for (i = 0; i < MAX_FDS; i++) {
        if (table->files[i]) {
            file_put(table->files[i]);
            table->files[i] = NULL;
        }
    }
}

fd_t fd_table_alloc(fd_table_t* table, file_t* file) {
    u64 flags;
    size_t i;

    if (!table || !file) {
        return FD_INVALID;
    }

    spin_lock_irqsave(&table->lock, &flags);

    for (i = 0; i < MAX_FDS; i++) {
        if (table->files[i] == NULL) {
            table->files[i] = file;

            // fd table holds a reference
            file_get(file);

            spin_unlock_irqrestore(&table->lock, flags);

            return (fd_t)i;
        }
    }

    spin_unlock_irqrestore(&table->lock, flags);

    return FD_INVALID;
}

file_t* fd_table_get(fd_table_t* table, fd_t fd) {
    u64 flags;
    file_t* file;

    if (!table) {
        return NULL;
    }

    if (fd < 0 || fd >= MAX_FDS) {
        return NULL;
    }

    spin_lock_irqsave(&table->lock, &flags);

    file = table->files[fd];

    if (file) {
        file_get(file);
    }

    spin_unlock_irqrestore(&table->lock, flags);

    return file;
}

int fd_table_close(fd_table_t* table, fd_t fd) {
    u64 flags;
    file_t* file;

    if (!table) {
        return -EINVAL;
    }

    if (fd < 0 || fd >= MAX_FDS) {
        return -EBADF;
    }

    spin_lock_irqsave(&table->lock, &flags);

    file = table->files[fd];

    if (!file) {
        spin_unlock_irqrestore(&table->lock, flags);
        return -EBADF;
    }

    // detach the FD while holding the table lock
    table->files[fd] = NULL;

    spin_unlock_irqrestore(&table->lock, flags);

    // do not run the file's final close callback while holding the FD table lock
    file_put(file);

    return 0;
}

int fd_table_clone(fd_table_t* dst, fd_table_t* src) {
    if (!dst || !src || dst == src) {
        return -EINVAL;
    }

    file_t* files[MAX_FDS];
    memset(files, 0, sizeof(file_t*) * MAX_FDS);
    
    // take references to a stable snapshot of the source, release its lock before locking the dst
    u64 flags = 0;
    spin_lock_irqsave(&src->lock, &flags);

    for (size_t i = 0; i < MAX_FDS; ++i) {
        files[i] = src->files[i];

        if (files[i]) {
            file_get(files[i]);
        }
    }

    spin_unlock_irqrestore(&src->lock, flags);

    spin_lock_irqsave(&dst->lock, &flags);
    
    // cloning is only supported into an empty uninitialized table
    for (size_t i = 0; i < MAX_FDS; ++i) {
        if (dst->files[i] != NULL) {
            spin_unlock_irqrestore(&dst->lock, flags);

            for (size_t j = 0; j < MAX_FDS; ++j) {
                if (files[j]) {
                    file_put(files[j]);
                }
            }

            return -EINVAL;
        }
    }

    // transfer snapshot references into the dst table, each populated slot now owns exactly one ref
    for (size_t i = 0; i < MAX_FDS; ++i) {
        dst->files[i] = files[i];
    }

    spin_unlock_irqrestore(&dst->lock, flags);

    return 0;
}