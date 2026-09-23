[bits 64]

global barrier_dma_read
barrier_dma_read:
    lfence
    ret

global barrier_dma_write
barrier_dma_write:
    sfence
    ret