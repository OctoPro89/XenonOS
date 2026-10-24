#include "vmm.h"
#include <xlibc/xassert.h>
#include <xlibc/string.h>
#include <memory/heap.h>
#include <memory/paging.h>
#include <limits.h>
#include <errno.h>

#define VMM_ALLOC_BASE  0xFFFFE00000000000ULL
#define VMM_ALLOC_SIZE  (1024ULL * 1024ULL * 1024ULL) // 1 GiB

#define VMM_PAGE_COUNT  (VMM_ALLOC_SIZE / PAGE_SIZE)
#define BITMAP_SIZE     (VMM_PAGE_COUNT / 8)

#define BIT_SET(i)   (vmm_bitmap[(i)/8] |=  (1 << ((i)%8)))
#define BIT_CLEAR(i) (vmm_bitmap[(i)/8] &= ~(1 << ((i)%8)))
#define BIT_TEST(i)  (vmm_bitmap[(i)/8] &   (1 << ((i)%8)))

static uint8_t vmm_bitmap[BITMAP_SIZE];

vmm_space_t kernel_space;

// TODO: this has a bug with the page flags
static pte_t* get_table(vmm_space_t* space, pte_t* table, uint16_t index, int create) {
    if (!(table[index] & PAGE_PRESENT)) {
        if (!create) { return NULL; }

        paddr_t phys = pmm_alloc_page();
        if (!phys) { return NULL; }
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
    if (!table) { return; }
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
    if (!phys) { return NULL; }
    pte_t* new_pml4 = (pte_t*)phys_to_hhdm(phys);

    memset((void*)new_pml4, 0, PAGE_SIZE);

    // copy kernel half (higher half entries)
    for (int i = 256; i < 512; i++) {
        new_pml4[i] = kernel_space.pml4[i];
    }

    vmm_space_t* space = kmalloc(sizeof(vmm_space_t));
    if (!space) {
        pmm_free_page(phys);
        return NULL;
    }

    memset(space, 0, sizeof(vmm_space_t));
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
    if (!space) { return; }
    asm volatile("mov %0, %%cr3" :: "r"(hhdm_to_phys((u64)space->pml4)));
}

int vmm_map(vmm_space_t* space, vaddr_t virt, paddr_t phys, uint64_t flags) {
    if (!space || !space->pml4) {
        return -EINVAL;
    }

    if ((virt & (PAGE_SIZE - 1)) != 0 || (phys & (PAGE_SIZE - 1)) != 0) {
        return -EINVAL;
    }

    pte_t* pml4 = space->pml4;

    pte_t* pdpt = get_table(space, pml4, pml4_index(virt), 1);
    if (!pdpt) {
        return -ENOMEM;
    }

    pte_t* pd = get_table(space, pdpt, pdpt_index(virt), 1);
    if (!pd) {
        return -ENOMEM;
    }

    pte_t* pt = get_table(space, pd, pd_index(virt), 1);
    if (!pt) {
        return -ENOMEM;
    }

    u64 index = pt_index(virt);

    // don't silently overwrite a physical page that's already mapped
    if (pt[index] & PAGE_PRESENT) {
        return -EEXIST;
    }

    pt[index] = phys | flags | PAGE_PRESENT;

    asm volatile("invlpg (%0)" :: "r"(virt) : "memory");

    return 0;
}

// TODO: this will need to be changed for SMP
void vmm_unmap(vmm_space_t* space, vaddr_t virt) {
    if (!space) { return; }

    pte_t* pml4 = space->pml4;

    pte_t* pdpt = get_table_noalloc(pml4, pml4_index(virt));
    if (!pdpt) { return; }

    pte_t* pd = get_table_noalloc(pdpt, pdpt_index(virt));
    if (!pd) { return; }

    pte_t* pt = get_table_noalloc(pd, pd_index(virt));
    if (!pt) { return; }

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
    if (!space || !space->pml4 || pages == 0) {
        return 0;
    }

    if ((addr & (PAGE_SIZE - 1)) != 0) {
        return 0;
    }

    // prevent overflow when calculating the range
    if (pages > UINT64_MAX / PAGE_SIZE) {
        return 0;
    }

    u64 bytes = (u64)pages * PAGE_SIZE;

    if (addr > UINT64_MAX - (bytes - 1)) {
        return 0;
    }

    vaddr_t virt = vmm_alloc_virtual_pages(pages);
    if (!virt) {
        return 0;
    }

    size_t mapped = 0;

    for (; mapped < pages; mapped++) {
        if (vmm_map(space, virt + (mapped * PAGE_SIZE), addr + (mapped * PAGE_SIZE), flags) != 0) {
            // roll back all successfully mapped pages
            for (size_t j = 0; j < mapped; j++) {
                vmm_unmap(space, virt + (j * PAGE_SIZE));
            }

            vmm_free_virtual_pages(virt, pages);
            return 0;
        }
    }

    return virt;
}


void vmm_unmap_pages(vmm_space_t* space, vaddr_t virt, size_t pages){
    if (!space) { return; }

    for (size_t i = 0; i < pages; i++) {
        vmm_unmap(space, virt + (i * PAGE_SIZE));
    }

    vmm_free_virtual_pages(virt, pages);
}

vaddr_t vmm_map_physical_page(vmm_space_t* space, paddr_t phys, uint64_t flags) {
    if (!space) { return 0; }

    vaddr_t virt_page = vmm_alloc_virtual_pages(1);
    if (!virt_page) {
        return 0;
    }

    if (vmm_map(space, virt_page, phys, flags) != 0) { return 0; }
    return virt_page;
}

b8 vmm_map_mmio(vmm_space_t* space, vaddr_t virt, paddr_t phys, size_t size){
    if (!space) { return false; }

    size = (size + 0xFFF) & ~0xFFF;

    for (size_t off = 0; off < size; off += 0x1000) {
        if (vmm_map(space, virt + off, phys + off, PAGE_PRESENT | PAGE_WRITABLE | PAGE_CACHE_DISABLE) != 0) { return false; }
    }

    return true;
}

paddr_t vmm_virt_to_phys(vmm_space_t* space, vaddr_t virt) {
    if (!space) { return 0; }
    
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

int vmm_update_page_flags(vmm_space_t* space, vaddr_t virt, u64 set_flags, u64 clear_flags) {
    if (!space || !space->pml4 || (virt & (PAGE_SIZE - 1)) != 0) {
        return -EINVAL;
    }

    pte_t* pml4 = space->pml4;

    pte_t* pdpt = get_table_noalloc(pml4, pml4_index(virt));
    if (!pdpt) { return -ENOENT; }

    pte_t* pd = get_table_noalloc(pdpt, pdpt_index(virt));
    if (!pd) { return -ENOENT; }

    pte_t* pt = get_table_noalloc(pd, pd_index(virt));
    if (!pt) { return -ENOENT; }

    pte_t* entry = &pt[pt_index(virt)];

    if (!(*entry & PAGE_PRESENT)) {
        return -ENOENT;
    }

    *entry &= ~clear_flags;
    *entry |= set_flags;

    asm volatile("invlpg (%0)" :: "r"(virt) : "memory");

    return 0;
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

static void rollback_non_contiguous(vmm_space_t* space, vaddr_t base, size_t mapped_bytes) {
    size_t pages_to_cleanup = mapped_bytes / PAGE_SIZE;

    for (size_t i = 0; i < pages_to_cleanup; ++i) {
        vaddr_t curr_vaddr = base + (i * PAGE_SIZE);
        
        paddr_t phys = vmm_virt_to_phys(space, curr_vaddr);
        
        if (phys != 0) {
            vmm_unmap(space, curr_vaddr);
            pmm_free_page(phys);
        }
    }
}

static int vmm_page_is_mapped(vmm_space_t* space, vaddr_t virt) {
    pte_t* pdpt;
    pte_t* pd;
    pte_t* pt;
    pte_t entry;

    if (!space || !space->pml4) {
        return 1;
    }

    pdpt = get_table_noalloc(space->pml4, pml4_index(virt));
    if (!pdpt) {
        return 0;
    }

    entry = space->pml4[pml4_index(virt)];
    if (!(entry & PAGE_PRESENT)) {
        return 0;
    }

    entry = pdpt[pdpt_index(virt)];
    if (!(entry & PAGE_PRESENT)) {
        return 0;
    }

    // Bit 7 is PS in a PDPTE: 1 GiB page.
    if (entry & (1ULL << 7)) {
        return 1;
    }

    pd = get_table_noalloc(pdpt, pdpt_index(virt));
    if (!pd) {
        return 0;
    }

    entry = pd[pd_index(virt)];
    if (!(entry & PAGE_PRESENT)) {
        return 0;
    }

    // Bit 7 is PS in a PDE: 2 MiB page.
    if (entry & (1ULL << 7)) {
        return 1;
    }

    pt = get_table_noalloc(pd, pd_index(virt));
    if (!pt) {
        return 0;
    }

    return (pt[pt_index(virt)] & PAGE_PRESENT) != 0;
}

vaddr_t vmm_find_free_region(vmm_space_t* space, size_t size) {
    size_t pages;
    size_t run = 0;
    size_t start = 0;
    size_t i;

    if (!space || !space->pml4 || size == 0) {
        return 0;
    }

    pages = size / PAGE_SIZE;
    if (size % PAGE_SIZE) {
        pages++;
    }

    if (pages > VMM_PAGE_COUNT) {
        return 0;
    }

    for (i = 0; i < VMM_PAGE_COUNT; i++) {
        vaddr_t virt = VMM_ALLOC_BASE + i * PAGE_SIZE;

        if (!BIT_TEST(i) && !vmm_page_is_mapped(space, virt)) {
            if (run == 0) {
                start = i;
            }

            run++;

            if (run == pages) {
                size_t j;

                for (j = start; j < start + pages; j++) {
                    BIT_SET(j);
                }

                return VMM_ALLOC_BASE + start * PAGE_SIZE;
            }
        } else {
            run = 0;
        }
    }

    return 0;
}

// TODO: check
int vmm_alloc_stack(vmm_space_t* space, size_t usable_pages, u16 guard_pages, vaddr_t* out_base, vaddr_t* out_top) {
    if (usable_pages == 0 || !out_base || !out_top) {
        return -EINVAL;
    }

    u64 flags = PAGE_PRESENT | PAGE_WRITABLE;

    size_t total_pages = usable_pages + guard_pages;
    size_t total_size = total_pages * PAGE_SIZE;

    vaddr_t vaddr_start = vmm_find_free_region(space, total_size);
    if (vaddr_start == 0) {
        return -ENOMEM;
    }

    vaddr_t base = vaddr_start + (guard_pages * PAGE_SIZE); 
    size_t mapped_bytes = 0;

    for (size_t i = 0; i < usable_pages; ++i) {
        paddr_t phys = pmm_alloc_page();
        if (phys == 0) {
            rollback_non_contiguous(space, base, mapped_bytes);
            vmm_free_virtual_pages(vaddr_start, total_pages);
            return -ENOMEM;
        }

        memset((void*)phys_to_hhdm(phys), 0, PAGE_SIZE);

        if (vmm_map(space, base + (i * PAGE_SIZE), phys, flags) != 0) {
            pmm_free_page(phys);
            rollback_non_contiguous(space, base, mapped_bytes);
            vmm_free_virtual_pages(vaddr_start, total_pages);
            return -EFAULT;
        }

        mapped_bytes += PAGE_SIZE;
    }

    *out_base = base;
    *out_top = base + (usable_pages * PAGE_SIZE);
    return 0;
}
