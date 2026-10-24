#pragma once
#include <xlibc/xstdint.h>
#include <memory/memory_types.h>
#include <memory/paging_arch.h>
#include <kernel.h>

#define HHDM_OFFSET 0xFFFF800000000000ULL

// NOTE: These are direct physical memory access helpers,
// NOT intended to be real virtual memory translation
static inline vaddr_t phys_to_hhdm(paddr_t phys) {
    return (vaddr_t)phys + HHDM_OFFSET;
}

static inline paddr_t hhdm_to_phys(vaddr_t virt) {
    return (paddr_t)virt - HHDM_OFFSET;
}

__PRIVILEGED_CODE extern ASMCALL void tlb_flush_all();

__PRIVILEGED_CODE static __hint_inline__ void invlpg(vaddr_t addr) {
    asm volatile("invlpg (%0)" :: "r"(addr) : "memory");
}

__PRIVILEGED_CODE static __hint_inline__ u64 paging_read_cr3() {
    u64 val;
    asm volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}

__PRIVILEGED_CODE static __hint_inline__ paddr_t paging_get_kernel_pt_root() {
    return paging_read_cr3() & ~0xFFFULL;
}