#pragma once

#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <windowing/window/window.h>
#include <windowing/window/surface.h>
#include <memory/ring_buffer.h>
#include <drivers/input/input.h>

#define WINDOW_MAX_COUNT 16
#define WINDOW_EVENT_QUEUE_SIZE 64

typedef enum {
    WINDOW_EVENT_FLAGS_NONE = 0,
    WINDOW_EVENT_FLAGS_KEYBOARD = (1 << 0)
} window_event_flags_t;

struct window {
    window_id_t id;
    window_rect_t rect;
    window_type_t type;
    window_state_t state;
    u32 flags;

    window_event_flags_t e_flags;

    char title[WINDOW_TITLE_MAX];

    b8 alive;
    b8 focused;

    const window_surface_t* surface;
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

    const window_surface_t* surface;
    window_t* handle; // non-owning, valid only while window lifetime is guaranteed
} window_snapshot_t;

/**
 * @brief Copies the current visible-window state in back-to-front order for the compositor
 * @returns The number of windows
 */
u32 window_server_snapshot(window_snapshot_t* out, u32 capacity);

typedef void (*window_invalidate_callback_t)(void);

/**
 * @brief Internal dirty-frame notification
 */
void window_server_set_invalidate_callback(window_invalidate_callback_t callback);

/**
 * @brief Tells a window that its damage has been rendered
 * @note Called by the compositor in `compositor_render()`
 */
void window_clear_damage(window_t* window);

/**
 * @brief Queues `evt` to all windows subscribed to keyboard events
 */
void window_server_notify_keyboard_subscribers(input_keyboard_event_t* evt);

/**
 * @brief Subscribes `window` to keyboard events
 */
void window_server_subscribe_to_keyboard(window_t* window);