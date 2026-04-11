#pragma once
#include <xlibc/xstdint.h>
#include <memory/pmm.h>
#include <memory/paging_arch.h>

typedef struct vmm_space {
    pte_t* pml4;
} vmm_space_t;

extern vmm_space_t kernel_space;

void vmm_init(vmm_space_t* space, pte_t* kernel_pml4);

void vmm_map(vmm_space_t* space, VIRTUAL_ADDRESS virt, PHYSICAL_ADDRESS phys, uint64_t flags);
void vmm_unmap(vmm_space_t* space, VIRTUAL_ADDRESS virt);

void vmm_map_mmio(vmm_space_t* space, VIRTUAL_ADDRESS virt, PHYSICAL_ADDRESS phys, size_t size);

PHYSICAL_ADDRESS vmm_virt_to_phys(vmm_space_t* space, VIRTUAL_ADDRESS virt);