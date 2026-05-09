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