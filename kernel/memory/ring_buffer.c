#include <memory/ring_buffer.h>
#include <arch/x86_64/sync/sync.h>
#include <xlibc/string.h>
#include <xlibc/stdlib.h>
#include <kernel.h>

[[nodiscard]]
ring_buffer_t* ring_buffer_create(size_t capacity, size_t elem_size) {
    if (capacity == 0 || elem_size == 0) {
        return NULL;
    }

    /* Prevent multiplication from overflowing. */
    if (capacity > SIZE_MAX / elem_size) {
        return NULL;
    }

    ring_buffer_t* rb = (ring_buffer_t*)kmalloc(sizeof(ring_buffer_t));
    if (!rb) {
        return NULL;
    }

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

    spin_lock_init(&rb->lock);

    return rb;
}

/*
 * The caller must ensure no threads, tasks, or interrupt handlers
 * can access the buffer before destroying it.
 */
void ring_buffer_destroy(ring_buffer_t* rb) {
    if (!rb) {
        return;
    }

    kfree(rb->data);
    kfree(rb);
}

void ring_buffer_clear(ring_buffer_t* rb) {
    if (!rb) {
        return;
    }

    u64 flags;
    spin_lock_irqsave(&rb->lock, &flags);

    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;

    spin_unlock_irqrestore(&rb->lock, flags);
}

b8 ring_buffer_push(ring_buffer_t* rb, const void* elem) {
    if (!rb || !elem) {
        return false;
    }

    u64 flags;
    spin_lock_irqsave(&rb->lock, &flags);

    if (rb->count == rb->capacity) {
        spin_unlock_irqrestore(&rb->lock, flags);
        return false;
    }

    memcpy(
        rb->data + rb->head * rb->elem_size,
        elem,
        rb->elem_size
    );

    rb->head++;
    if (rb->head == rb->capacity) {
        rb->head = 0;
    }

    rb->count++;

    spin_unlock_irqrestore(&rb->lock, flags);
    return true;
}

b8 ring_buffer_pop(ring_buffer_t* rb, void* elem) {
    if (!rb || !elem) {
        return false;
    }

    u64 flags;
    spin_lock_irqsave(&rb->lock, &flags);

    if (rb->count == 0) {
        spin_unlock_irqrestore(&rb->lock, flags);
        return false;
    }

    memcpy(
        elem,
        rb->data + rb->tail * rb->elem_size,
        rb->elem_size
    );

    rb->tail++;
    if (rb->tail == rb->capacity) {
        rb->tail = 0;
    }

    rb->count--;

    spin_unlock_irqrestore(&rb->lock, flags);
    return true;
}

/*
 * These functions accept a non-const buffer because acquiring
 * its lock modifies synchronization state.
 */
b8 ring_buffer_peek(ring_buffer_t* rb, void* elem) {
    if (!rb || !elem) {
        return false;
    }

    u64 flags;
    spin_lock_irqsave(&rb->lock, &flags);

    if (rb->count == 0) {
        spin_unlock_irqrestore(&rb->lock, flags);
        return false;
    }

    memcpy(
        elem,
        rb->data + rb->tail * rb->elem_size,
        rb->elem_size
    );

    spin_unlock_irqrestore(&rb->lock, flags);
    return true;
}

b8 ring_buffer_peek_back(ring_buffer_t* rb, void* elem) {
    if (!rb || !elem) {
        return false;
    }

    u64 flags;
    spin_lock_irqsave(&rb->lock, &flags);

    if (rb->count == 0) {
        spin_unlock_irqrestore(&rb->lock, flags);
        return false;
    }

    size_t index = (rb->head == 0)
        ? rb->capacity - 1
        : rb->head - 1;

    memcpy(
        elem,
        rb->data + index * rb->elem_size,
        rb->elem_size
    );

    spin_unlock_irqrestore(&rb->lock, flags);
    return true;
}