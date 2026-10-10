#pragma once

#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <windowing/window/window.h>
#include <windowing/window/surface.h>
#include <memory/ring_buffer.h>

#define WINDOW_MAX_COUNT 16
#define WINDOW_EVENT_QUEUE_SIZE 64

struct window {
    window_id_t id;
    window_rect_t rect;
    window_type_t type;
    window_state_t state;
    u32 flags;

    char title[WINDOW_TITLE_MAX];

    b8 alive;
    b8 focused;

    window_surface_t* surface; // borrowed pointer for now
    ring_buffer_t* events;

    b8 damaged;
    window_rect_t damage;
};

typedef struct {
    window_id_t id;
    window_rect_t rect;
    window_type_t type;
    window_state_t state;
    u32 flags;

    char title[WINDOW_TITLE_MAX];
    b8 focused;

    window_surface_t* surface; // borroed, pixel memory is not copied
    window_t* handle; // non-owning, valid only while window lifetime is guaranteed
} window_snapshot_t;

/**
 * @brief Copies the current visible-window state in back-to-front order for the compositor
 * @returns 
 */
u32 window_server_snapshot(window_snapshot_t* out, u32 capacity);

typedef void (*window_invalidate_callback_t)(void);

/**
 * @brief Internal dirty-frame notification
 */
void window_server_set_invalidate_callback(window_invalidate_callback_t callback);