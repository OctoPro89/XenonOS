#include <libwindow/window_client.h>
#include <arch/x86_64/syscall.h>

ssize_t window_create(const window_config_t* config) {
    long ret;

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_WINDOW_CREATE),
          "D"(config)
        : "rcx", "r11", "r10", "memory"
    );

    return (ssize_t)ret;
}

ssize_t window_destroy() {
    long ret;

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_WINDOW_DESTROY)
        : "rcx", "r11", "r10", "memory"
    );

    return (ssize_t)ret;
}

ssize_t window_damage(const window_rect_t* rect) {
    long ret;

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_WINDOW_DAMAGE),
          "D"(rect)
        : "rcx", "r11", "r10", "memory"
    );

    return (ssize_t)ret;
}

ssize_t window_memcpy_frame(const void* user_buffer, size_t size) {
    long ret;

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_GETCWD),
          "D"(user_buffer),
          "S"(size)
        : "rcx", "r11", "r10", "memory"
    );

    return (ssize_t)ret;
}

ssize_t window_get_event(window_event_t* evt) {
    long ret;

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_WINDOW_POP_EVENT),
          "D"(evt)
        : "rcx", "r11", "r10", "memory"
    );

    return (ssize_t)ret;
}

ssize_t window_setup_terminal_io() {
    long ret;

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_WINDOW_SETUP_TERMINAL_IO)
        : "rcx", "r11", "r10", "memory"
    );

    return (ssize_t)ret;
}