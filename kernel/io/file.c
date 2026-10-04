#include <io/file.h>
#include <xlibc/stdlib.h>

static void file_destroy(file_t* file) {
    if (file->ops && file->ops->close) {
        file->ops->close(file);
    }

    kfree(file);
}

void file_init(file_t* file, const file_ops_t* ops, void* private) {
    file->ops = ops;
    file->private = private;
    file->flags = 0;
    file->refcount = 1;
}

void file_get(file_t* file) {
    if (!file) {
        return;
    }

    __atomic_add_fetch(&file->refcount, 1, __ATOMIC_RELAXED);
}

void file_put(file_t* file) {
    if (!file) {
        return;
    }

    if (__atomic_sub_fetch(&file->refcount, 1, __ATOMIC_ACQ_REL) == 0) {
        file_destroy(file);
    }
}