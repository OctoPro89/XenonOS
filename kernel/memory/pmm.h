#pragma once
#include <xlibc/xstdint.h>
#include <stddef.h>
#include "memory_types.h"
#include "../../shared/boot_info.h"

void pmm_init(BootInfo* boot);

PHYSICAL_ADDRESS pmm_alloc_page(void);
void pmm_free_page(PHYSICAL_ADDRESS page);

void pmm_mark_used(PHYSICAL_ADDRESS start, size_t size);
void pmm_mark_free(PHYSICAL_ADDRESS start, size_t size);

size_t pmm_total_pages(void);
size_t pmm_used_pages(void);