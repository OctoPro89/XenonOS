#pragma once

#include <xlibc/xstdint.h>

typedef enum {
    WINDOW_EVENT_CLOSE_REQUEST,
    WINDOW_EVENT_REDRAW,
    WINDOW_EVENT_MOVED,
    WINDOW_EVENT_RESIZED,
    WINDOW_EVENT_MOUSE_DOWN,
    WINDOW_EVENT_MOUSE_UP,
    WINDOW_EVENT_MOUSE_MOVE,
    WINDOW_EVENT_KEY_DOWN,
    WINDOW_EVENT_KEY_UP,
} window_event_type_t;

typedef struct {
    window_event_type_t type;
    u32 buttons;
    u32 keycode;
} window_event_t;

typedef struct window window_t;