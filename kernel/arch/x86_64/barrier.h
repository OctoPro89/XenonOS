#pragma once
#include <xlibc/xstdint.h>
#include <kernel.h>

// DMA read barrier, ensures CPU sees device DMA writes
extern ASMCALL void barrier_dma_read();

// DMA write barrier, ensures device sees CPU writes
extern ASMCALL void barrier_dma_write();