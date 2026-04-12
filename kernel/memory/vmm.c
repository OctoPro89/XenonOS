#include "vmm.h"
#include <memory/paging.h>
#include <xlibc/string.h>
#include <memory/heap.h>

vmm_space_t kernel_space;

static pte_t* get_table(vmm_space_t* space, pte_t* table, uint16_t index, int create) {
    if (!(table[index] & PAGE_PRESENT)) {
        if (!create) return NULL;

        PHYSICAL_ADDRESS phys = pmm_alloc_page();
        pte_t* virt = (pte_t*)phys_to_hhdm(phys);

        for (int i = 0; i < 512; i++) { virt[i] = 0; }

        u64 flags = PAGE_PRESENT | PAGE_WRITABLE;

        if (space->user_mode) {
            flags |= PAGE_USER;
        }

        table[index] = phys | flags;
    }

    PHYSICAL_ADDRESS phys = table[index] & ~0xFFFULL;
    return (pte_t*)phys_to_hhdm(phys);
}

static pte_t* get_table_noalloc(pte_t* table, uint16_t index) {
    if (!(table[index] & PAGE_PRESENT)) { return NULL; }

    PHYSICAL_ADDRESS phys = table[index] & ~0xFFFULL;
    return (pte_t*)phys_to_hhdm(phys);
}

vmm_space_t* vmm_create_space() {
    PHYSICAL_ADDRESS phys = pmm_alloc_page();
    pte_t* new_pml4 = (pte_t*)phys_to_hhdm(phys);

    memset((void*)new_pml4, 0, PAGE_SIZE);

    // copy kernel half (higher half entries)
    for (int i = 256; i < 512; i++) {
        new_pml4[i] = kernel_space.pml4[i];
    }

    vmm_space_t* space = kmalloc(sizeof(vmm_space_t));
    space->pml4 = new_pml4;

    return space;
}

void vmm_switch(vmm_space_t* space) {
    asm volatile("mov %0, %%cr3" :: "r"(hhdm_to_phys((u64)space->pml4)));
}

void vmm_map(vmm_space_t* space, VIRTUAL_ADDRESS virt, PHYSICAL_ADDRESS phys, uint64_t flags)
{
    pte_t* pml4 = space->pml4;

    pte_t* pdpt = get_table(space, pml4, pml4_index(virt), 1);
    pte_t* pd = get_table(space, pdpt, pdpt_index(virt), 1);
    pte_t* pt = get_table(space, pd, pd_index(virt), 1);

    pt[pt_index(virt)] = phys | flags | PAGE_PRESENT;

    asm volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

void vmm_unmap(vmm_space_t* space, VIRTUAL_ADDRESS virt)
{
    pte_t* pml4 = space->pml4;

    pte_t* pdpt = get_table_noalloc(pml4, pml4_index(virt));
    if (!pdpt) return;

    pte_t* pd = get_table_noalloc(pdpt, pdpt_index(virt));
    if (!pd) return;

    pte_t* pt = get_table_noalloc(pd, pd_index(virt));
    if (!pt) return;

    pt[pt_index(virt)] = 0;

    asm volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

void vmm_map_mmio(vmm_space_t* space, VIRTUAL_ADDRESS virt, PHYSICAL_ADDRESS phys, size_t size)
{
    size = (size + 0xFFF) & ~0xFFF;

    for (size_t off = 0; off < size; off += 0x1000) {
        vmm_map(space, virt + off, phys + off, PAGE_WRITABLE | PAGE_CACHE_DISABLE);
    }
}

PHYSICAL_ADDRESS vmm_virt_to_phys(vmm_space_t* space, VIRTUAL_ADDRESS virt) {
    pte_t* pml4 = space->pml4;

    pte_t* pdpt = get_table_noalloc(pml4, pml4_index(virt));
    if (!pdpt) { return 0; }

    pte_t* pd = get_table_noalloc(pdpt, pdpt_index(virt));
    if (!pd) { return 0; }

    pte_t* pt = get_table_noalloc(pd, pd_index(virt));
    if (!pt) { return 0; }

    if (!(pt[pt_index(virt)] & PAGE_PRESENT)) { return 0; }

    return (pt[pt_index(virt)] & ~0xFFFULL) | (virt & 0xFFF);
}