#include "heap.h"
#include <memory/vmm.h>
#include <memory/pmm.h>
#include <xlibc/string.h>
#include <xlibc/xassert.h>

/*
    *** IMPORTANT NOTE ***
    The current block header is likely 24 bytes on x86_64 so the returned
    pointers are probably only 8 byte aligned instead of 16 bytes. This may
    cause problems with SIMD
*/

#define HEAP_START 0xFFFFA00000000000ULL
#define HEAP_END   0xFFFFA00010000000ULL

typedef struct heap_block {
    size_t size;
    int free;
    struct heap_block* next;
} heap_block;

static heap_block* heap_head = NULL;
static vaddr_t heap_ptr = HEAP_START;

static void* heap_expand(size_t size)
{
    size_t total = size + sizeof(heap_block);
    size_t pages = (total + 0xFFF) / PAGE_SIZE;

    xassert(heap_ptr + pages * PAGE_SIZE < HEAP_END, "Invalid heap_expand()");

    vaddr_t start = heap_ptr;

    for (size_t i = 0; i < pages; i++) {
        paddr_t phys = pmm_alloc_page();

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

void* krealloc(void* ptr, size_t size)
{
    if (!ptr) {
        return kmalloc(size);
    }

    if (!size) {
        kfree(ptr);
        return NULL;
    }

    // match kmalloc's 16-byte allocation-size rounding
    if (size > SIZE_MAX - 15) {
        return NULL;
    }

    size = (size + 15) & ~(size_t)15;

    heap_block* block = (heap_block*)((uint8_t*)ptr - sizeof(heap_block));

    // shrink in place, or retain the existing block if the unused tail would be too small to split
    if (block->size >= size) {
        split(block, size);
        return ptr;
    }

    // try to grow into the immediately following free block
    heap_block* next = block->next;

    if (next && next->free && next->size <= SIZE_MAX - sizeof(heap_block) - block->size && block->size + sizeof(heap_block) + next->size >= size) {
        block->size += sizeof(heap_block) + next->size;
        block->next = next->next;

        split(block, size);
        return ptr;
    }

    // fallback allocate, copy, and free
    void* new_ptr = kmalloc(size);

    if (!new_ptr) {
        return NULL;
    }

    size_t copy_size = block->size;

    if (copy_size > size) {
        copy_size = size;
    }

    memcpy(new_ptr, ptr, copy_size);
    kfree(ptr);

    return new_ptr;
}