#include <xlibc/stdlib.h>
#include <xlibc/xstddef.h>
#include <xlibc/xstdint.h>
#include <xlibc/string.h>
#include <limits.h>
#include <arch/x86_64/syscall.h>
#include <memory/paging.h>
#include <unistd.h>

#define MALLOC_ALIGNMENT 16u

typedef struct block {
    size_t size; // payload capacity excluding metadata
    int free;
    struct block* next;
} block_t;

#define HEADER_SIZE \
    ((sizeof(block_t) + MALLOC_ALIGNMENT - 1) & \
     ~(size_t)(MALLOC_ALIGNMENT - 1))

static block_t* heap_head;
static block_t* heap_tail;

static int align_size(size_t size, size_t* result) {
    if (size > (size_t)-1 - (MALLOC_ALIGNMENT - 1)) {
        return 0;
    }

    *result = (size + MALLOC_ALIGNMENT - 1) & ~(size_t)(MALLOC_ALIGNMENT - 1);
    return 1;
}

static block_t* request_block(size_t size) {
    if (size > (size_t)-1 - HEADER_SIZE) {
        return NULL;
    }

    size_t total = size + HEADER_SIZE;

    if (total > (size_t)-1 - (PAGE_SIZE - 1)) {
        return NULL;
    }

    size_t chunk = (total + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);

    if (chunk > (size_t)LONG_MAX) {
        return NULL;
    }

    ssize_t ret = (ssize_t)sbrk((ssize_t)chunk);

    // raw syscall errors are negative
    if (ret < 0) {
        return NULL;
    }

    block_t* block = (block_t*)(uintptr_t)ret;

    block->size = chunk - HEADER_SIZE;
    block->free = 1;
    block->next = NULL;

    if (heap_tail) {
        heap_tail->next = block;
    }
    else {
        heap_head = block;
    }

    heap_tail = block;
    return block;
}

void* malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    size_t wanted;
    if (!align_size(size, &wanted)) {
        return NULL;
    }

    for (block_t* block = heap_head; block; block = block->next) {
        if (!block->free || block->size < wanted) {
            continue;
        }

        // split only if the remainder can hold metadata and at least one aligned payload unit
        if (block->size - wanted >= HEADER_SIZE + MALLOC_ALIGNMENT) {
            block_t* remainder = (block_t*)((char*)block + HEADER_SIZE + wanted);

            remainder->size = block->size - wanted - HEADER_SIZE;
            remainder->free = 1;
            remainder->next = block->next;

            block->next = remainder;
            block->size = wanted;

            if (heap_tail == block) {
                heap_tail = remainder;
            }
        }

        block->free = 0;
        return (char*)block + HEADER_SIZE;
    }

    block_t* block = request_block(wanted);
    if (!block) {
        return NULL;
    }

    block->free = 0;
    return (char*)block + HEADER_SIZE;
}

void free(void* ptr) {
    if (!ptr) {
        return;
    }

    block_t* block = (block_t*)((char*)ptr - HEADER_SIZE);
    block->free = 1;

    // merge adjacent free blocks, blocks are kept in heap order
    for (block_t* current = heap_head; current && current->next;) {
        block_t* next = current->next;

        char* current_end = (char*)current + HEADER_SIZE + current->size;

        if (current->free && next->free && current_end == (char*)next) {
            current->size += HEADER_SIZE + next->size;
            current->next = next->next;

            if (heap_tail == next) {
                heap_tail = current;
            }
        } else {
            current = next;
        }
    }
}