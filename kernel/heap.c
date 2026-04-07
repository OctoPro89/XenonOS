#include "heap.h"
#include <paging.h>

typedef struct heap_block {
    size_t size;
    int free;
    struct heap_block* next;
} heap_block;

static heap_block* heap_head = NULL;

static void* heap_expand(size_t size) {
    size_t total = size + sizeof(heap_block);

    // round up to page size
    size_t pages = (total + 0xFFF) / 0x1000;

    void* mem = alloc_and_map_identity();
    for (size_t i = 1; i < pages; i++)
        alloc_and_map_identity(); // contiguous assumption (fine for now)

    heap_block* block = (heap_block*)mem;
    block->size = pages * 0x1000 - sizeof(heap_block);
    block->free = 1;
    block->next = NULL;

    return block;
}

static heap_block* find_free_block(size_t size) {
    heap_block* curr = heap_head;

    while (curr) {
        if (curr->free && curr->size >= size)
            return curr;
        curr = curr->next;
    }

    return NULL;
}

static void split_block(heap_block* block, size_t size) {
    if (block->size <= size + sizeof(heap_block))
        return;

    heap_block* new_block = (heap_block*)
        ((uint8_t*)block + sizeof(heap_block) + size);

    new_block->size = block->size - size - sizeof(heap_block);
    new_block->free = 1;
    new_block->next = block->next;

    block->size = size;
    block->next = new_block;
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;

    // align to 16 bytes
    size = (size + 15) & ~15;

    heap_block* block;

    if (!heap_head) {
        block = heap_expand(size);
        heap_head = block;
    } else {
        block = find_free_block(size);

        if (!block) {
            block = heap_expand(size);

            // append
            heap_block* curr = heap_head;
            while (curr->next) curr = curr->next;
            curr->next = block;
        }
    }

    split_block(block, size);
    block->free = 0;

    return (void*)((uint8_t*)block + sizeof(heap_block));
}

void kfree(void* ptr) {
    if (!ptr) return;

    heap_block* block =
        (heap_block*)((uint8_t*)ptr - sizeof(heap_block));

    block->free = 1;

    // coalesce
    heap_block* curr = heap_head;

    while (curr && curr->next) {
        if (curr->free && curr->next->free) {
            curr->size += sizeof(heap_block) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}