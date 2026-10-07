#include <io/terminal_file.h>

#include <xlibc/stdlib.h>
#include <xlibc/string.h>

static ssize_t terminal_file_read(file_t* file, void* buffer, size_t size) {
    if (!file || !buffer || size == 0) {
        return 0;
    }

    terminal_t* terminal = (terminal_t*)file->private;
    if (!terminal) {
        return -1;
    }

    return terminal_read(terminal, buffer, size);
}

static ssize_t terminal_file_write(file_t* file, const void* buffer, size_t size) {
    if (!file || !buffer || size == 0) {
        return 0;
    }

    terminal_t* terminal = (terminal_t*)file->private;
    if (!terminal) {
        return -1;
    }

    return terminal_write(terminal, buffer, size);
}

static int terminal_file_close(file_t* file) {
    // the terminal itself is owned by the kernel, so closing an fd must not destroy it
    (void)file;
    return 0;
}

static const file_ops_t terminal_file_ops = {
    .read = terminal_file_read,
    .write = terminal_file_write,
    .close = terminal_file_close,
};

file_t* terminal_file_create(terminal_t* terminal) {
    if (!terminal) {
        return NULL;
    }

    file_t* file = kmalloc(sizeof(file_t));

    if (!file) {
        return NULL;
    }

    file->ops = &terminal_file_ops;
    file->private = terminal;
    file->flags = 0;
    file->refcount = 1;

    return file;
}