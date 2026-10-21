#pragma once
#include <xlibc/xstdint.h>
#include <kernel.h>

// DMA read barrier, ensures CPU sees device DMA writes
extern ASMCALL void barrier_dma_read();

// DMA write barrier, ensures device sees CPU writes
extern ASMCALL void barrier_dma_write();

// IO read barrier, ensures MMIO read completion
static __hint_inline__ void barrier_io_read() {
    asm volatile("" ::: "memory");
}

// IO write barrier, ensures MMIO write ordering
static __hint_inline__ void barrier_io_write() {
    asm volatile("" ::: "memory");
}