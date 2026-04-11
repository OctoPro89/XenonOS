#pragma once
#include <xlibc/xstdint.h>
#include <memory/memory_types.h>
#include <memory/paging_arch.h>

#define HHDM_OFFSET 0xFFFF800000000000ULL

// NOTE: These are direct physical memory access helpers,
// NOT intended to be real virtual memory translation
static inline VIRTUAL_ADDRESS phys_to_hhdm(PHYSICAL_ADDRESS phys) {
    return (VIRTUAL_ADDRESS)phys + HHDM_OFFSET;
}

static inline PHYSICAL_ADDRESS hhdm_to_phys(VIRTUAL_ADDRESS virt) {
    return (PHYSICAL_ADDRESS)virt - HHDM_OFFSET;
}