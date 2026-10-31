#include <logging.h>
#include <xlibc/string.h>
#include <xlibc/stdio.h>
#include <xlibc/xstdarg.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/sync/sync.h>
#include <kernel.h>

static const struct kernel_log_backend* current_backend = NULL;

__PRIVILEGED_DATA static spinlock_t g_log_lock;

// level prefixes
static const char* level_prefixes[] = {
    "[KERNEL DEBUG]: ",
    "[KERNEL INFO]: ",
    "[KERNEL WARN]: ",
    "[KERNEL ERROR]: ",
    "[KERNEL FATAL]: "
};

void kernel_log_init() {
    spin_lock_init(&g_log_lock);
}

void kernel_log_set_backend(const struct kernel_log_backend* be) {
    u64 flags = 0;
    spin_lock_irqsave(&g_log_lock, &flags);
    current_backend = be;
    spin_unlock_irqrestore(&g_log_lock, flags);
}

static __hint_inline__ void output(const char* str, size_t len) {
    if (current_backend && current_backend->write) {
        current_backend->write(str, len);
    }
    else {
        for (size_t i = 0; i < len; ++i) {
            serial_write_char(str[i]);
        }
    }
}

static void do_kernel_log(int level, const char* fmt, va_list args) {
    char fmt_buffer[256];
    char out_buffer[512];

    vsnprintf(fmt_buffer, sizeof(fmt_buffer), fmt, args);
    int written = snprintf(out_buffer, sizeof(out_buffer), "%s%s\n", level_prefixes[level], fmt_buffer);

    u64 flags = 0;
    spin_lock_irqsave(&g_log_lock, &flags);
    output(out_buffer, (size_t)written);
    spin_unlock_irqrestore(&g_log_lock, flags);
}

void __kernel_log_debug(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    do_kernel_log(KERNEL_LOG_LEVEL_DEBUG, fmt, args);
    va_end(args);
}

void __kernel_log_info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    do_kernel_log(KERNEL_LOG_LEVEL_INFO, fmt, args);
    va_end(args);
}

void __kernel_log_warning(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    do_kernel_log(KERNEL_LOG_LEVEL_WARN, fmt, args);
    va_end(args);
}

void __kernel_log_error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    do_kernel_log(KERNEL_LOG_LEVEL_ERROR, fmt, args);
    va_end(args);
}

__attribute__((noreturn)) void __kernel_log_fatal(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    do_kernel_log(KERNEL_LOG_LEVEL_FATAL, fmt, args);
    va_end(args);

    // halt execution entirely
    while (1) {
        __asm__ volatile("cli; hlt");
    }
}