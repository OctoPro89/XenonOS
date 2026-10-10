#pragma once

#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <xlibc/xassert.h>
#include <kernel.h>

#define INPUT_KBD_ACTION_DOWN ((u8)0)
#define INPUT_KBD_ACTION_UP ((u8)1)

#define INPUT_MOUSE_FLAG_RELATIVE ((u8)1u << 0)

typedef struct __packed__ {
    u8 action;
    u8 modifiers;
    u16 usage;
    u8 reserved[4];
} input_keyboard_event_t;

typedef struct __packed__ {
    i32 x_value;
    i32 y_value;
    i16 wheel;
    u16 buttons;
    u8 flags;
    u8 reserved[3];
} input_mouse_event_t;

STATIC_ASSERT(sizeof(input_keyboard_event_t) == 8);
STATIC_ASSERT(sizeof(input_mouse_event_t) == 16);

extern u32 crnt_mouse_x;
extern u32 crnt_mouse_y;

/**
 * @brief Initialize the input subsystem
 */
b8 input_init();

/**
 * @brief Enqueue a keyboard event. Non-blocking, drops on overflow.
 * @return Number of events enqueued (true = 1) or false = 0 on overflow
 */
b8 input_push_keyboard_event(const input_keyboard_event_t* evt);

/**
 * @brief Enqueue a mouse event. Non-blocking, drops on overflow.
 * @return Number of events enqueued (true = 1) or false = 0 on overflow
 */
b8 input_push_mouse_event(const input_mouse_event_t* evt);

/**
 * @brief Consumer side API for consuming a single keyboard event
 * @returns True on success else false
 */
b8 input_pop_keyboard_event(input_keyboard_event_t* evt);

/**
 * @brief Consumer side API for consuming a single mouse keyboard event
 * @returns True on success else false
 */
b8 input_pop_mouse_event(input_mouse_event_t* evt);