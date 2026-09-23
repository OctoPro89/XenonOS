#include "dma_allocator.h"
#include <memory/vmm.h>
#include <memory/pmm.h>
#include <xlibc/xassert.h>

#define KERNEL_DMA_BASE 0xFFFFA00000000000ULL

static vaddr_t dma_next = KERNEL_DMA_BASE;

dma_region_t dma_alloc(size_t size)
{
    size_t aligned = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    size_t pages = aligned / PAGE_SIZE;

    vaddr_t vaddr = dma_next;
    dma_next += aligned;

    paddr_t phys = pmm_alloc_contiguous_pages(pages);

    xassert(phys != 0, "dma_alloc(): out of physical memory");

    for (size_t i = 0; i < pages; i++) {
        vmm_map(
            &kernel_space,
            vaddr + i * PAGE_SIZE,
            phys + i * PAGE_SIZE,
            PAGE_PRESENT | PAGE_WRITABLE
        );
    }

    // TODO: optional but useful for safety
    // memset((void*)vaddr, 0, aligned);

    dma_region_t region = {
        .virt = vaddr,
        .phys = phys,
        .size = aligned
    };

    return region;
}

// TODO: don't use bump allocator, dma_next isn't decreased
/*
void dma_free(dma_region_t region)
{
    size_t pages = region.size / PAGE_SIZE;

    for (size_t i = 0; i < pages; i++) {
        vmm_unmap(
            &kernel_space,
            region.virt + i * PAGE_SIZE
        );
    }

    pmm_free_pages(region.phys, pages);
}
*/