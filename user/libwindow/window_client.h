#pragma once

#include <windowing/window/windefs.h>
#include <windowing/window/events.h>
#include <xlibc/xstddef.h>

// TODO: these operate on the current process, for now processes can only have one window
ssize_t window_create(const window_config_t* config);
ssize_t window_destroy();
ssize_t window_damage(const window_rect_t* rect);
ssize_t window_memcpy_frame(const void* user_buffer, size_t size);
ssize_t window_get_event(window_event_t* evt);
ssize_t window_setup_terminal_io();
ssize_t window_subscribe_keyboard_events();