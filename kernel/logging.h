#pragma once

#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>

typedef enum {
    KERNEL_LOG_LEVEL_DEBUG = 0,
    KERNEL_LOG_LEVEL_INFO = 1,
    KERNEL_LOG_LEVEL_WARN = 2,
    KERNEL_LOG_LEVEL_ERROR = 3,
    KERNEL_LOG_LEVEL_FATAL = 4
} kernel_log_level_t;

// backend interface for late-stage kernel
struct kernel_log_backend {
    void (*write)(const char* data, size_t len);
};

/**
 * @brief Initializes logging subsystem
 */
void kernel_log_init();

/**
 * @brief Set custom backend (NULL = serial)
 */
void kernel_log_set_backend(const struct kernel_log_backend* be);

/**
 * @brief Log a debug message.
 */
void __kernel_log_debug(const char* fmt, ...);

/**
 * @brief Log an info message.
 */
void __kernel_log_info(const char* fmt, ...);

/**
 * @brief Log a warning message.
 */
void __kernel_log_warning(const char* fmt, ...);

/**
 * @brief Log an error message.
 */
void __kernel_log_error(const char* fmt, ...);

/**
 * @brief Log a fatal error and halt the system.
 */
__attribute__((noreturn)) void __kernel_log_fatal(const char* fmt, ...);


#ifdef KERNEL_LOG_DEBUG
#define kernel_log_debug(...) ((void)0)
#else
#define kernel_log_debug(...) __kernel_log_debug(__VA_ARGS__)
#endif

#ifdef KERNEL_LOG_INFO
#define kernel_log_info(...) ((void)0)
#else
#define kernel_log_info(...) __kernel_log_info(__VA_ARGS__)
#endif

#ifdef KERNEL_LOG_WARN
#define kernel_log_warn(...) ((void)0)
#else
#define kernel_log_warn(...) __kernel_log_warn(__VA_ARGS__)
#endif

#ifdef KERNEL_LOG_ERROR
#define kernel_log_error(...) ((void)0)
#else
#define kernel_log_error(...) __kernel_log_error(__VA_ARGS__)
#endif

#define kernel_log_fatal(...) __kernel_log_fatal(__VA_ARGS__)