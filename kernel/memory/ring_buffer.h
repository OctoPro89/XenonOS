#pragma once

#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <arch/x86_64/sync/sync.h>

/**
 * @note Uses spinlocks for synchronization and is thread-safe for push / pop etc
 */
typedef struct {
    u8* data;
    size_t capacity;
    size_t elem_size;
    size_t head; // write position
    size_t tail; // read position
    size_t count; // number of elements currently stored
    spinlock_t lock;
} ring_buffer_t;

/**
 * Allocate and initialize a ring buffer.
 * @return Ring buffer pointer on success, NULL on allocation failure
 */
[[nodiscard]] ring_buffer_t* ring_buffer_create(size_t capacity, size_t elem_size);

/**
 * Destroys a ring buffer, freeing memory
 */
void ring_buffer_destroy(ring_buffer_t* rb);

/**
 * Remove all elements without freeing the buffer
 */
void ring_buffer_clear(ring_buffer_t* rb);

/*
 * Add/remove one element.
 *
 * push/pop return false if the operation cannot be performed:
 *   push -> buffer full
 *   pop  -> buffer empty
 */
b8 ring_buffer_push(ring_buffer_t* rb, const void* elem);
b8 ring_buffer_pop(ring_buffer_t* rb, void* elem);

/**
 * Look at the oldest / newest element without removing it.
 */
b8 ring_buffer_peek(ring_buffer_t* rb, void* elem);
b8 ring_buffer_peek_back(ring_buffer_t* rb, void* elem);