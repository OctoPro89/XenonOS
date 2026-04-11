#include "dma_allocator.h"
#include <memory/vmm.h>
#include <memory/pmm.h>
#include <xlibc/xassert.h>

#define KERNEL_DMA_BASE 0xFFFFA00000000000ULL

static VIRTUAL_ADDRESS dma_next = KERNEL_DMA_BASE;

dma_region_t dma_alloc(size_t size)
{
    xassert(size < PAGE_SIZE, "dma_alloc(): DMA memory must be contiguous and therefore may not be bigger than PAGE_SIZE bytes!");
    size = (size + 0xFFF) & ~0xFFF;

    VIRTUAL_ADDRESS vaddr = dma_next;
    dma_next += size;

    PHYSICAL_ADDRESS first_phys = 0;

    for (size_t off = 0; off < size; off += 0x1000) {
        PHYSICAL_ADDRESS phys = pmm_alloc_page();

        if (off == 0) { first_phys = phys; }

        vmm_map(&kernel_space, vaddr + off, phys, PAGE_WRITABLE);
    }

    dma_region_t region = {
        .virt = vaddr,
        .phys = first_phys,
        .size = size
    };

    return region;
}