#pragma once

#include <xlibc/xstdint.h>
#include <windowing/window/surface.h>
#include <windowing/window/windefs.h>

#define WINDOW_TITLE_MAX 64

typedef struct window window_t;
typedef u32 window_id_t;

window_t* window_server_create_window(const window_config_t* config);
void window_server_destroy_window(window_t* window);

void window_server_set_title(window_t* window, const char* title);
void window_server_set_position(window_t* window, i32 x, i32 y);

window_rect_t window_server_get_rect(const window_t* window);

b8 window_server_set_surface(window_t* window, const window_surface_t* surface);

void window_server_request_close(window_t* window);

/**
 * @brief Raise a window
 */
void window_server_raise(window_t* window);

/**
 * @brief Tell the compositor which content region changed
 */
void window_server_damage(window_t* window, i32 x, i32 y, i32 width, i32 height);