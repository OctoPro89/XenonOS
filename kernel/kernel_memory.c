#include "kernel_memory.h"

#define ALIGN16(x) (((x) + 15) & ~15)

typedef struct block {
    size_t size;
    int free;
    struct block* next;
} block_t;

static block_t* free_list = NULL;

void kheap_init(void* start, size_t size) {
    free_list = (block_t*)start;
    free_list->size = size - sizeof(block_t);
    free_list->free = 1;
    free_list->next = NULL;
}

void* kmalloc(size_t size) {
    size = ALIGN16(size);
    block_t* curr = free_list;

    while (curr) {
        if (curr->free && curr->size >= size) {

            // Split if large enough
            if (curr->size > size + sizeof(block_t)) {
                block_t* new_block = (block_t*)((uint8_t*)curr + sizeof(block_t) + size);

                new_block->size = curr->size - size - sizeof(block_t);
                new_block->free = 1;
                new_block->next = curr->next;

                curr->next = new_block;
                curr->size = size;
            }

            curr->free = 0;
            return (void*)((uint8_t*)curr + sizeof(block_t));
        }

        curr = curr->next;
    }

    return NULL; // no space
}

void kfree(void* ptr) {
    if (!ptr) return;

    block_t* block = (block_t*)((uint8_t*)ptr - sizeof(block_t));
    block->free = 1;

    // Coalesce
    block_t* curr = free_list;

    while (curr && curr->next) {
        if (curr->free && curr->next->free) {
            curr->size += sizeof(block_t) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}