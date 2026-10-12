#pragma once

#include <xlibc/xstdint.h>

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

typedef struct window_config {
    const char* title;
    window_rect_t rect;
    window_type_t type;
    u32 flags;
} window_config_t;

typedef u32 window_surface_handle_t;
#define WINDOW_SURFACE_INVALID_HANDLE ((window_surface_handle_t)-1)