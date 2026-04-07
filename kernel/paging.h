#pragma once

#include <xlibc/xstdint.h>

#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_USER     (1ULL << 2)
#define PAGE_NX (1ULL << 63) // no-exec

#define PML4_INDEX(x) (((x) >> 39) & 0x1FF)
#define PDPT_INDEX(x) (((x) >> 30) & 0x1FF)
#define PD_INDEX(x)   (((x) >> 21) & 0x1FF)
#define PT_INDEX(x)   (((x) >> 12) & 0x1FF)

extern uint64_t* current_pml4;

// TODO: currently a POS, need to fix later

void* alloc_page(void);
void* alloc_and_map_identity(void);
void map_page(uint64_t* pml4, uint64_t virt, uint64_t phys, uint64_t flags);
uint64_t* create_address_space(uint64_t* kernel_pml4);