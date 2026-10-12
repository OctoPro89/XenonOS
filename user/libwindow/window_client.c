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