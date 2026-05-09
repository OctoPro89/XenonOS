#pragma once
#include <xlibc/xstdint.h>
#include <stddef.h>
#include "memory_types.h"
#include "../../shared/boot_info.h"

void pmm_init(BootInfo* boot);

paddr_t pmm_alloc_page(void);
void pmm_free_page(paddr_t page);

paddr_t pmm_alloc_contiguous_pages(size_t count);
void pmm_free_countiguous_pages(paddr_t addr, size_t count);

void pmm_mark_used(paddr_t start, size_t size);
void pmm_mark_free(paddr_t start, size_t size);

size_t pmm_total_pages(void);
size_t pmm_used_pages(void);