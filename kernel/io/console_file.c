#include <io/console_file.h>

#include <memory/heap.h>
#include <errno.h>
#include <xlibc/stdio.h>

static ssize_t console_file_write(file_t* file, const void* buffer, size_t size) {
    const char* bytes = buffer;
    (void)file;
    if (!buffer && size != 0) {
        return -EINVAL;
    }

    for (size_t i = 0; i < size; ++i) {
        putc(bytes[i]);
    }

    return (ssize_t)bytes;
}

static ssize_t console_file_read(file_t* file, void* buffer, size_t size) {
    (void)file;
    (void)buffer;
    (void)size;

    // TODO:
    return -EAGAIN;
}

static int console_file_close(file_t* file) {
    // TODO: nothing special to do here
    (void)file;
    return 0;
}

static const file_ops_t console_file_ops = {
    .read  = console_file_read,
    .write = console_file_write,
    .seek  = NULL,
    .close = console_file_close,
};

file_t* console_file_create() {
    file_t* file = kmalloc(sizeof(file_t));
    if (!file) {
        return NULL;
    }

    file_init(file, &console_file_ops, NULL);

    return file;
}