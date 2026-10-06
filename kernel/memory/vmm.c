#include "vmm.h"
#include <xlibc/xassert.h>
#include <xlibc/string.h>
#include <memory/heap.h>
#include <memory/paging.h>

#define VMM_ALLOC_BASE  0xFFFFE00000000000ULL
#define VMM_ALLOC_SIZE  (1024ULL * 1024ULL * 1024ULL) // 1 GiB

#define VMM_PAGE_COUNT  (VMM_ALLOC_SIZE / PAGE_SIZE)
#define BITMAP_SIZE     (VMM_PAGE_COUNT / 8)

#define BIT_SET(i)   (vmm_bitmap[(i)/8] |=  (1 << ((i)%8)))
#define BIT_CLEAR(i) (vmm_bitmap[(i)/8] &= ~(1 << ((i)%8)))
#define BIT_TEST(i)  (vmm_bitmap[(i)/8] &   (1 << ((i)%8)))

static uint8_t vmm_bitmap[BITMAP_SIZE];

vmm_space_t kernel_space;

static pte_t* get_table(vmm_space_t* space, pte_t* table, uint16_t index, int create) {
    if (!(table[index] & PAGE_PRESENT)) {
        if (!create) return NULL;

        paddr_t phys = pmm_alloc_page();
        pte_t* virt = (pte_t*)phys_to_hhdm(phys);

        for (int i = 0; i < 512; i++) { virt[i] = 0; }

        u64 flags = PAGE_PRESENT | PAGE_WRITABLE;

        if (space->user_mode) {
            flags |= PAGE_USER;
        }

        table[index] = phys | flags;
    }

    paddr_t phys = table[index] & ~0xFFFULL;
    return (pte_t*)phys_to_hhdm(phys);
}

static pte_t* get_table_noalloc(pte_t* table, uint16_t index) {
    if (!(table[index] & PAGE_PRESENT)) { return NULL; }

    paddr_t phys = table[index] & ~0xFFFULL;
    return (pte_t*)phys_to_hhdm(phys);
}

static void destroy_table(pte_t* table, int level) {
    for (int i = 0; i < 512; ++i) {
        pte_t entry = table[i];

        if (!(entry & PAGE_PRESENT)) {
            continue;
        }

        paddr_t phys = entry & ~0xFFFULL;
        if (level == 1) {
            pmm_free_page(phys);
            continue;
        }

        pte_t* child = (pte_t*)phys_to_hhdm(phys);
        destroy_table(child, level - 1);
        pmm_free_page(phys);
    }   
}

vmm_space_t* vmm_create_space() {
    paddr_t phys = pmm_alloc_page();
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

void vmm_destroy_space(vmm_space_t* space) {
    if (!space) {
        return;
    }

    // user mappings occupy PML4 entries 0 through 255,
    // kernel entries 256 through 511 are shared and must NOT be freeds
    for (int i = 0; i < 256; ++i) {
        if (!(space->pml4[i] & PAGE_PRESENT)) {
            continue;
        }

        paddr_t phys = space->pml4[i] & ~0xFFFULL;
        pte_t* table = (pte_t*)phys_to_hhdm(phys);
        destroy_table(table, 3);
        pmm_free_page(phys);
    }

    paddr_t pml4_phys = hhdm_to_phys((u64)space->pml4);
    pmm_free_page(pml4_phys);
    kfree(space);
}

void vmm_switch(vmm_space_t* space) {
    asm volatile("mov %0, %%cr3" :: "r"(hhdm_to_phys((u64)space->pml4)));
}

void vmm_map(vmm_space_t* space, vaddr_t virt, paddr_t phys, uint64_t flags) {
    pte_t* pml4 = space->pml4;

    pte_t* pdpt = get_table(space, pml4, pml4_index(virt), 1);
    pte_t* pd = get_table(space, pdpt, pdpt_index(virt), 1);
    pte_t* pt = get_table(space, pd, pd_index(virt), 1);

    pt[pt_index(virt)] = phys | flags | PAGE_PRESENT;

    asm volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

void vmm_unmap(vmm_space_t* space, vaddr_t virt) {
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

vaddr_t vmm_alloc_virtual_pages(size_t pages) {
    size_t run = 0;
    size_t start = 0;

    for (size_t i = 0; i < VMM_PAGE_COUNT; i++) {

        if (!BIT_TEST(i)) {

            if (run == 0) {
                start = i;
            }

            run++;

            if (run == pages) {

                for (size_t j = start; j < start + pages; j++) {
                    BIT_SET(j);
                }

                return VMM_ALLOC_BASE + (start * PAGE_SIZE);
            }

        } else {
            run = 0;
        }
    }

    return 0;
}

void vmm_free_virtual_pages(vaddr_t addr, size_t pages) {
    size_t index = (addr - VMM_ALLOC_BASE) / PAGE_SIZE;

    for (size_t i = 0; i < pages; i++) {
        BIT_CLEAR(index + i);
    }
}

vaddr_t vmm_map_physically_contiguous_pages(vmm_space_t* space, paddr_t addr, size_t pages, uint64_t flags) {
    vaddr_t virt = vmm_alloc_virtual_pages(pages);

    if (!virt) {
        return 0;
    }

    for (size_t i = 0; i < pages; i++) {

        vmm_map(
            space,
            virt + (i * PAGE_SIZE),
            addr + (i * PAGE_SIZE),
            flags
        );
    }

    return virt;
}

void vmm_unmap_pages(vmm_space_t* space, vaddr_t virt, size_t pages){
    for (size_t i = 0; i < pages; i++) {
        vmm_unmap(space, virt + (i * PAGE_SIZE));
    }

    vmm_free_virtual_pages(virt, pages);
}

vaddr_t vmm_map_physical_page(vmm_space_t* space, paddr_t phys, uint64_t flags) {
    vaddr_t virt_page = vmm_alloc_virtual_pages(1);
    if (!virt_page) {
        return 0;
    }

    vmm_map(space, virt_page, phys, flags);
    return virt_page;
}

void vmm_map_mmio(vmm_space_t* space, vaddr_t virt, paddr_t phys, size_t size){
    size = (size + 0xFFF) & ~0xFFF;

    for (size_t off = 0; off < size; off += 0x1000) {
        vmm_map(space, virt + off, phys + off, PAGE_WRITABLE | PAGE_CACHE_DISABLE);
    }
}

paddr_t vmm_virt_to_phys(vmm_space_t* space, vaddr_t virt) {
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

b8 vmm_user_range_valid(vmm_space_t* space, vaddr_t addr, size_t size, b8 write) {
    if (!space || !space->user_mode) {
        return false;
    }

    if (size == 0) {
        return true;
    }

    // check for overflow and reject non-user addresses
    if (addr > VMM_USER_MAX) {
        return false;
    }

    if ((size - 1) > (VMM_USER_MAX - addr)) {
        return false;
    }

    vaddr_t first = addr & ~(PAGE_SIZE - 1);
    vaddr_t last = (addr + size - 1) & ~(PAGE_SIZE - 1);

    for (vaddr_t page = first;; page += PAGE_SIZE) {
        pte_t* pml4 = space->pml4;
        pte_t* pdpt = get_table_noalloc(pml4, pml4_index(page));
        if (!pdpt) {
            return false;
        }

        pte_t* pd = get_table_noalloc(pdpt, pdpt_index(page));
        if (!pd) {
            return false;
        }

        pte_t* pt = get_table_noalloc(pd, pd_index(page));

        if (!pt) {
            return false;
        }

        pte_t entry = pt[pt_index(page)];
        if (!(entry & PAGE_PRESENT)) {
            return false;
        }

        if (!(entry & PAGE_USER)) {
            return false;
        }

        if (write && !(entry & PAGE_WRITABLE)) {
            return false;
        }

        if (page == last) {
            break;
        }
    }

    return true;
}