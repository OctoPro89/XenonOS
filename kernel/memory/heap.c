#include "heap.h"
#include <memory/vmm.h>
#include <memory/pmm.h>
#include <xlibc/string.h>
#include <xlibc/xassert.h>

#define HEAP_START 0xFFFFA00000000000ULL
#define HEAP_END   0xFFFFA00010000000ULL

typedef struct heap_block {
    size_t size;
    int free;
    struct heap_block* next;
} heap_block;

static heap_block* heap_head = NULL;
static VIRTUAL_ADDRESS heap_ptr = HEAP_START;

static void* heap_expand(size_t size)
{
    size_t total = size + sizeof(heap_block);
    size_t pages = (total + 0xFFF) / PAGE_SIZE;

    xassert(heap_ptr + pages * PAGE_SIZE < HEAP_END, "Invalid heap_expand()");

    VIRTUAL_ADDRESS start = heap_ptr;

    for (size_t i = 0; i < pages; i++) {
        PHYSICAL_ADDRESS phys = pmm_alloc_page();

        vmm_map(&kernel_space, heap_ptr, phys, PAGE_PRESENT | PAGE_WRITABLE);

        heap_ptr += PAGE_SIZE;
    }

    heap_block* block = (heap_block*)start;
    block->size = pages * PAGE_SIZE - sizeof(heap_block);
    block->free = 1;
    block->next = NULL;

    return block;
}

static heap_block* find_free(size_t size)
{
    heap_block* curr = heap_head;

    while (curr) {
        if (curr->free && curr->size >= size) { return curr; }
        curr = curr->next;
    }

    return NULL;
}

static void split(heap_block* block, size_t size)
{
    if (block->size <= size + sizeof(heap_block)) { return; }

    heap_block* newb = (heap_block*)((uint8_t*)block + sizeof(heap_block) + size);

    newb->size = block->size - size - sizeof(heap_block);
    newb->free = 1;
    newb->next = block->next;

    block->size = size;
    block->next = newb;
}

void* kmalloc(size_t size)
{
    if (!size) { return NULL; }

    size = (size + 15) & ~15;

    heap_block* block = find_free(size);

    if (!block) {
        block = heap_expand(size);

        if (!heap_head) { heap_head = block; }
        else {
            heap_block* curr = heap_head;
            while (curr->next) curr = curr->next;
            curr->next = block;
        }
    }

    split(block, size);
    block->free = 0;

    return (void*)((uint8_t*)block + sizeof(heap_block));
}

void kfree(void* ptr)
{
    if (!ptr) return;

    heap_block* block = (heap_block*)((uint8_t*)ptr - sizeof(heap_block));

    block->free = 1;

    heap_block* curr = heap_head;

    while (curr && curr->next) {
        if (curr->free && curr->next->free) {
            curr->size += sizeof(heap_block) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }

    ptr = NULL;
}