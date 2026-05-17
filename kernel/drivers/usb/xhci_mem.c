#include <drivers/usb/xhci_mem.h>
#include <memory/paging.h>
#include <memory/vmm.h>
#include <xlibc/xassert.h>
#include <xlibc/string.h>

vaddr_t xhci_map_mmio(paddr_t pci_bar_address, u32 bar_size) {
    size_t page_count = bar_size / PAGE_SIZE;
    vaddr_t vbase = vmm_map_physically_contiguous_pages(&kernel_space, pci_bar_address, page_count, PAGE_PRESENT | PAGE_WRITABLE | PAGE_CACHE_DISABLE);
    return vbase;
}

dma_region_t xhci_alloc_memory(size_t size, size_t alignment, size_t boundary) {
    xassert(size, "Attempted xchi DMA allocation with size 0!");
    xassert(alignment, "Attempted xchi DMA allocation with alignment 0!");
    xassert(boundary, "Attempted xchi DMA allocation with boundary 0!");
    
    dma_region_t memblock = dma_alloc(size); // TODO: use other parameters
    
    xassert(memblock.size, "XHCI memory allocation failed!");
    memset((void*)memblock.virt, 0, memblock.size);
    return memblock;
}

void xhci_free_memory(void* ptr) {
    // dma_free(ptr);
}