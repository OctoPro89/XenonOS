#pragma once
#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <memory/memory_types.h>

vaddr_t xhci_map_mmio(paddr_t pci_bar_address, u32 bar_size);

void* xhci_alloc_memory(size_t size, size_t alignment, size_t boundary);
void xhci_free_memory(void* ptr);
paddr_t xhci_get_physical_addr(vaddr_t vaddr);