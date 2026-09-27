#include <drivers/input/input.h>
#include <memory/ring_buffer.h>
#include <xlibc/stdio.h>

static ring_buffer_t* g_kbd_rb;
static ring_buffer_t* g_mouse_rb;

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
    
    return ring_buffer_push(g_mouse_rb, (const u8*)evt);
}