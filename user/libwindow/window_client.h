#pragma once

#include <windowing/window/windefs.h>
#include <xlibc/xstddef.h>

// TODO: these operate on the current process, for now processes can only have one window
ssize_t window_create(const window_config_t* config);
ssize_t window_destroy();
ssize_t window_setup_terminal_io();

/*
b8 window_server_set_title(window_handle_t window, const char* title);
b8 window_server_set_position(window_handle_t window, i32 x, i32 y);
b8 window_set_size(window_handle_t window, i32 width, i32 height);

b8 window_server_get_rect(window_handle_t window, window_rect_t* out_rect);

b8 window_server_request_close(window_handle_t window);
b8 window_server_raise(window_handle_t window);

b8 window_server_damage(window_handle_t window, i32 x, i32 y, i32 width, i32 height);
*/