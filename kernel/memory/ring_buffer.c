#include <memory/ring_buffer.h>
#include <xlibc/string.h>
#include <xlibc/stdlib.h>
#include <kernel.h>

static __hint_inline__ b8 ring_buffer_empty(const ring_buffer_t* rb) {
    return !rb || rb->count == 0;
}

static __hint_inline__ b8 ring_buffer_full(const ring_buffer_t* rb) {
    return rb && rb->count == rb->capacity;
}

[[nodiscard]] ring_buffer_t* ring_buffer_create(size_t capacity, size_t elem_size) {
    if (capacity == 0 || elem_size == 0) { return NULL; }

    // prevent multiplication from wrapping
    if (capacity > SIZE_MAX / elem_size) { return NULL; }

    ring_buffer_t* rb = (ring_buffer_t*)kmalloc(sizeof(ring_buffer_t));
    if (!rb) { return NULL; }

    rb->data = (u8*)kmalloc(capacity * elem_size);
    if (!rb->data) {
        kfree(rb);
        return NULL;
    }

    rb->capacity = capacity;
    rb->elem_size = elem_size;
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;

    return rb;
}

void ring_buffer_destroy(ring_buffer_t* rb) {
    if (!rb) { return; }

    kfree(rb->data);
    kfree(rb);
}

void ring_buffer_clear(ring_buffer_t* rb) {
    if (!rb) { return; }

    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
}

b8 ring_buffer_push(ring_buffer_t* rb, const void* elem) {
    if (!rb || !elem || ring_buffer_full(rb)) { return false; }

    memcpy(rb->data + rb->head * rb->elem_size, elem, rb->elem_size);

    rb->head++;

    if (rb->head == rb->capacity) { rb->head = 0; }

    rb->count++;

    return true;
}

b8 ring_buffer_pop(ring_buffer_t* rb, void* elem) {
    if (!rb || !elem || ring_buffer_empty(rb)) { return false; }

    memcpy(elem, rb->data + rb->tail * rb->elem_size, rb->elem_size);

    rb->tail++;

    if (rb->tail == rb->capacity) { rb->tail = 0; }

    rb->count--;

    return true;
}

b8 ring_buffer_peek(const ring_buffer_t* rb, void* elem) {
    if (!rb || !elem || ring_buffer_empty(rb)) { return false; }

    memcpy(elem, rb->data + rb->tail * rb->elem_size, rb->elem_size);

    return true;
}

b8 ring_buffer_peek_back(const ring_buffer_t* rb, void* elem) {
    if (!rb || !elem || ring_buffer_empty(rb)) { return false; }

    size_t index = (rb->head == 0) ? rb->capacity - 1 : rb->head - 1;
    memcpy(elem, rb->data + index * rb->elem_size, rb->elem_size);

    return true;
}