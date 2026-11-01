#pragma once

#include <windowing/window/windefs.h>
#include <windowing/window/events.h>
#include <xlibc/xstddef.h>

// TODO: these operate on the current process, for now processes can only have one window
ssize_t window_create(const window_config_t* config);
ssize_t window_destroy();
ssize_t window_set_title(const char* title);
ssize_t window_damage(const window_rect_t* rect);
ssize_t window_memcpy_frame(const void* user_buffer, size_t size);
ssize_t window_get_event(window_event_t* evt);
ssize_t window_setup_terminal_io();
ssize_t window_subscribe_keyboard_events();
ssize_t window_get_total_rect(window_rect_t* rect);
ssize_t window_get_surface_rect(window_rect_t* rect);

/**
 * @brief Creates a `window_rect_t` for `window_create()` so that the specified size will be the surface-renderable size. Ex. 400x400 will create a window with a 400x400 surface size
 * @returns A rect to be used in `window_create()`
 */
window_rect_t window_make_surface_rect(int x, int y, int width, int height);