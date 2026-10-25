#pragma once

#include <xlibc/xstdint.h>
#include <de/surface.h>
#include <windowing/window/window.h>

// TODO: move to another file
#define CRASH_PROCESS_NAME_MAX 32

typedef struct {
    char process_name[CRASH_PROCESS_NAME_MAX];
    uint64_t pid;

    uint64_t exception;
    uint64_t error_code;
    uint64_t rip;

    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rflags;
} compositor_crash_report_t;

/**
 * @brief Copies a report into the compositor's pending-crash queue, does not draw or allocate memory
 */
void compositor_report_crash(const compositor_crash_report_t* report);

b8 compositor_init(xenon_surface_t* target);

/**
 * @brief Mark the display as needing composition
 */
void compositor_invalidate();

/**
 * @brief Process window damage and present if needed
 */
void compositor_render();

void compositor_process_events();

typedef struct window window_t;

const window_surface_t* compositor_create_surface_for_window(window_t* window);
void compositor_release_surface(window_surface_handle_t surface_handle);

/**
 * @brief Sets the desktop wallpaper to an image at `path`
 * @returns True on success, false otherwise
 */
b8 compositor_set_wallpaper(const char* path);

/**
 * @brief Queues a wallpaper change, calling `callback` when done.
 */
// TODO: this crashes and I don't know why
// void compositor_queue_set_wallpaper(const char* path);

typedef b8 (*compositor_program_spawn_fn)(const char* absolute_path);

void compositor_set_program_spawn_fn(compositor_program_spawn_fn fn);

b8 compositor_handle_key_event(u16 keycode, b8 pressed);