#pragma once
#include <xlibc/xstdint.h>
#include <memory/pmm.h>
#include <memory/paging_arch.h>

#define VMM_USER_MAX 0x00007FFFFFFFFFFFULL

typedef struct vmm_space {
    pte_t* pml4;
    b8 user_mode;
} vmm_space_t;

extern vmm_space_t kernel_space;

vmm_space_t* vmm_create_space();
void vmm_destroy_space(vmm_space_t* space);

void vmm_switch(vmm_space_t* space);

/**
 * @brief Maps a page into `space`'s page tables.
 * @returns 0 on success, negative errno on failure
 */
int vmm_map(vmm_space_t* space, vaddr_t virt, paddr_t phys, uint64_t flags);
void vmm_unmap(vmm_space_t* space, vaddr_t virt);

vaddr_t vmm_alloc_virtual_pages(size_t pages);
void vmm_free_virtual_pages(vaddr_t addr, size_t pages);
vaddr_t vmm_map_physically_contiguous_pages(vmm_space_t* space, paddr_t addr, size_t pages, uint64_t flags);
void vmm_unmap_pages(vmm_space_t* space, vaddr_t virt, size_t pages);

vaddr_t vmm_map_physical_page(vmm_space_t* space, paddr_t phys, uint64_t flags);
b8 vmm_map_mmio(vmm_space_t* space, vaddr_t virt, paddr_t phys, size_t size);

paddr_t vmm_virt_to_phys(vmm_space_t* space, vaddr_t virt);

int vmm_update_page_flags(vmm_space_t* space, vaddr_t virt, u64 set_flags, u64 clear_flags);

b8 vmm_user_range_valid(vmm_space_t* space, vaddr_t addr, size_t size, b8 write);

/**
 * @brief Allocates a stack under `space`
 * @note Should only be used for kernel pages
 */
int vmm_alloc_stack(vmm_space_t* space, size_t usable_pages, u16 guard_pages, vaddr_t* out_base, vaddr_t* out_top);

vaddr_t vmm_find_free_region(vmm_space_t* space, size_t size);