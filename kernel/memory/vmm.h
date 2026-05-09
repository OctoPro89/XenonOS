#pragma once
#include <xlibc/xstdint.h>
#include <memory/pmm.h>
#include <memory/paging_arch.h>

typedef struct vmm_space {
    pte_t* pml4;
    b8 user_mode;
} vmm_space_t;

extern vmm_space_t kernel_space;

vmm_space_t* vmm_create_space();

void vmm_switch(vmm_space_t* space);

void vmm_map(vmm_space_t* space, vaddr_t virt, paddr_t phys, uint64_t flags);
void vmm_unmap(vmm_space_t* space, vaddr_t virt);

vaddr_t vmm_alloc_virtual_pages(size_t pages);
void vmm_free_virtual_pages(vaddr_t addr, size_t pages);
vaddr_t vmm_map_physically_contiguous_pages(vmm_space_t* space, paddr_t addr, size_t pages, uint64_t flags);
void vmm_unmap_pages(vmm_space_t* space, vaddr_t virt, size_t pages);

vaddr_t vmm_map_physical_page(vmm_space_t* space, paddr_t phys, uint64_t flags);
void vmm_map_mmio(vmm_space_t* space, vaddr_t virt, paddr_t phys, size_t size);

paddr_t vmm_virt_to_phys(vmm_space_t* space, vaddr_t virt);