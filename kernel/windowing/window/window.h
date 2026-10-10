#pragma once

#include <xlibc/xstdint.h>
#include <windowing/window/surface.h>

#define WINDOW_TITLE_MAX 64

typedef struct window window_t;
typedef u32 window_id_t;

typedef struct {
    i32 x;
    i32 y;
    i32 width;
    i32 height;
} window_rect_t;

typedef enum {
    WINDOW_TYPE_NORMAL
} window_type_t;

typedef enum {
    WINDOW_STATE_NORMAL,
} window_state_t;

typedef struct {
    const char* title;
    window_rect_t rect;
    window_type_t type;
    u32 flags;
} window_config_t;

window_t* window_create(const window_config_t* config);
void window_destroy(window_t* window);

void window_set_title(window_t* window, const char* title);
void window_set_position(window_t* window, i32 x, i32 y);

window_rect_t window_get_rect(const window_t* window);

b8 window_set_surface(window_t* window, window_surface_t* surface);
window_surface_t* window_get_surface(window_t* window);

void window_request_close(window_t* window);

/**
 * @brief Raise a window
 */
void window_raise(window_t* window);

/**
 * @brief Tell the compositor which content region changed
 */
void window_damage(window_t* window, i32 x, i32 y, i32 width, i32 height);

/**
 * @brief **FOR NOW** Request a new composite frame
 */
void window_commit(window_t* window);