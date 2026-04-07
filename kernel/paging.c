#include "paging.h"

static uint64_t next_free_page = 0x1000000; // 16MB, adjust if needed

void* alloc_page(void) {
    void* page = (void*)next_free_page;
    next_free_page += 0x1000;

    // zero it
    for (int i = 0; i < 4096; i++)
        ((uint8_t*)page)[i] = 0;

    return page;
}

void* alloc_and_map_identity(void) {
    void* phys = (void*)next_free_page;
    next_free_page += 0x1000;

    // zero it
    for (int i = 0; i < 4096; i++)
        ((uint8_t*)phys)[i] = 0;

    map_page(current_pml4, (uint64_t)phys, (uint64_t)phys,
        PAGE_PRESENT | PAGE_WRITABLE);

    return phys;
}

void map_page(uint64_t* pml4, uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t* pdpt;
    uint64_t* pd;
    uint64_t* pt;

    uint64_t user = flags & PAGE_USER;

    // --- PML4 ---
    if (!(pml4[PML4_INDEX(virt)] & PAGE_PRESENT)) {
        pdpt = alloc_page();
        pml4[PML4_INDEX(virt)] = (uint64_t)pdpt | PAGE_PRESENT | PAGE_WRITABLE | user;
    } else {
        pdpt = (uint64_t*)(pml4[PML4_INDEX(virt)] & ~0xFFF);
        pml4[PML4_INDEX(virt)] |= PAGE_USER;
    }

    // --- PDPT ---
    if (!(pdpt[PDPT_INDEX(virt)] & PAGE_PRESENT)) {
        pd = alloc_page();
        pdpt[PDPT_INDEX(virt)] = (uint64_t)pd | PAGE_PRESENT | PAGE_WRITABLE | user;
    } else {
        pd = (uint64_t*)(pdpt[PDPT_INDEX(virt)] & ~0xFFF);
        pdpt[PDPT_INDEX(virt)] |= PAGE_USER;
    }

    // --- PD ---
    if (!(pd[PD_INDEX(virt)] & PAGE_PRESENT)) {
        pt = alloc_page();
        pd[PD_INDEX(virt)] = (uint64_t)pt | PAGE_PRESENT | PAGE_WRITABLE | user;
    } else {
        pt = (uint64_t*)(pd[PD_INDEX(virt)] & ~0xFFF);
        pd[PD_INDEX(virt)] |= PAGE_USER;
    }

    // --- PT ---
    pt[PT_INDEX(virt)] =
    (phys & ~0xFFF) |
    flags |
    PAGE_PRESENT;

    pt[PT_INDEX(virt)] &= ~(1ULL << 63);
}

uint64_t* create_address_space(uint64_t* kernel_pml4) {
    uint64_t* new_pml4 = alloc_page();

    // Copy higher half (kernel space)
    for (int i = 256; i < 512; i++) {
        new_pml4[i] = kernel_pml4[i];
    }

    return new_pml4;
}