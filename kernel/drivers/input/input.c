#include <drivers/input/input.h>
#include <memory/ring_buffer.h>
#include <xlibc/stdio.h>
#include <arch/x86_64/io.h>

static ring_buffer_t* g_kbd_rb;
static ring_buffer_t* g_mouse_rb;

// TODO: mouse input ridiculously slow on real hardware using pop event 

u32 crnt_mouse_x;
u32 crnt_mouse_y;

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
    if (!g_kbd_rb) {
        return false;
    }

    return ring_buffer_push(g_kbd_rb, (const u8*)evt);
}

b8 input_push_mouse_event(const input_mouse_event_t* evt) {
    if (!g_mouse_rb) {
        return false;
    }

    crnt_mouse_x += evt->x_value;
    crnt_mouse_y += evt->y_value;

    return ring_buffer_push(g_mouse_rb, (const u8*)evt);
}

b8 input_pop_keyboard_event(input_keyboard_event_t* evt) {
    if (!g_kbd_rb) {
        return false;
    }

    return ring_buffer_pop(g_kbd_rb, (void*)evt);
}

b8 input_pop_mouse_event(input_mouse_event_t* evt) {
    if (!g_mouse_rb) {
        return false;
    }

    return ring_buffer_pop(g_mouse_rb, (void*)evt);
}