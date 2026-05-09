#pragma once
#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <memory/memory_types.h>

typedef struct {
    vaddr_t virt;
    PHYSICAL_CONTIGUOUS_BUFFER phys;
    size_t size;
} dma_region_t;

dma_region_t dma_alloc(size_t size);