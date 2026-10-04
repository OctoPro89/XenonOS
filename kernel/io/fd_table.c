#include <io/fd_table.h>
#include <errno.h>

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