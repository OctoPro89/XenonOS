#include <drivers/input/input.h>
#include <memory/ring_buffer.h>
#include <xlibc/stdio.h>
#include <arch/x86_64/sync/sync.h>
#include <task.h>
#include <windowing/window/internal.h>
#include <windowing/compositor/compositor.h>

static ring_buffer_t* g_kbd_rb;
static ring_buffer_t* g_mouse_rb;

static wait_queue_t g_kbd_waiters;
static spinlock_t g_kbd_wait_lock;

#define KBD_RING_CAPACITY 4096
#define MOUSE_RING_CAPACITY 4096

b8 input_init() {
    g_kbd_rb = ring_buffer_create(KBD_RING_CAPACITY, sizeof(input_keyboard_event_t));
    if (!g_kbd_rb) {
        printf("[INPUT]: Failed to create keyboard ring buffer\n");
        return false;
    }

    g_mouse_rb = ring_buffer_create(MOUSE_RING_CAPACITY, sizeof(input_mouse_event_t));
    if (!g_mouse_rb) {
        printf("[INPUT]: Failed to create mouse ring buffer\n");
        return false;
    }

    return true;
}

b8 input_push_keyboard_event(const input_keyboard_event_t* evt) {
    if (!g_kbd_rb || !evt) {
        return false;
    }

    u64 flags;
    spin_lock_irqsave(&g_kbd_wait_lock, &flags);

    b8 pushed = ring_buffer_push(g_kbd_rb, evt);

    if (pushed) {
        // TODO: decide who gets input
        if (compositor_handle_key_event(evt->usage, evt->action == INPUT_KBD_ACTION_DOWN)) {
            // consumed
        }
        else if (!window_server_notify_keyboard_subscribers(evt)) {
            wait_queue_wake_one_locked(&g_kbd_waiters);
        }
    }

    spin_unlock_irqrestore(&g_kbd_wait_lock, flags);

    return pushed;
}

b8 input_push_mouse_event(const input_mouse_event_t* evt) {
    if (!g_mouse_rb) {
        return false;
    }

    return ring_buffer_push(g_mouse_rb, (const u8*)evt);
}

b8 input_pop_keyboard_event(input_keyboard_event_t* evt) {
    if (!g_kbd_rb || !evt) {
        return false;
    }

    u64 flags;
    spin_lock_irqsave(&g_kbd_wait_lock, &flags);

    b8 popped = ring_buffer_pop(g_kbd_rb, evt);

    spin_unlock_irqrestore(&g_kbd_wait_lock, flags);

    return popped;
}

b8 input_pop_mouse_event(input_mouse_event_t* evt) {
    if (!g_mouse_rb) {
        return false;
    }

    return ring_buffer_pop(g_mouse_rb, (void*)evt);
}

b8 input_wait_pop_keyboard_event(input_keyboard_event_t* evt) {
    if (!g_kbd_rb || !evt) {
        return false;
    }

    for (;;) {
        u64 flags;
        spin_lock_irqsave(&g_kbd_wait_lock, &flags);

        if (ring_buffer_pop(g_kbd_rb, evt)) {
            spin_unlock_irqrestore(&g_kbd_wait_lock, flags);
            return true;
        }

        flags = task_wait(
            &g_kbd_waiters,
            &g_kbd_wait_lock,
            flags
        );

        spin_unlock_irqrestore(&g_kbd_wait_lock, flags);
    }
}