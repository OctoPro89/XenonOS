#include "pmm.h"
#include <memory/paging.h>
#include <xlibc/string.h>
#include <efi/efi.h>
#include <linker.ld.h>

#define BIT_SET(b, i)   ((b)[(i)/8] |=  (1 << ((i)%8)))
#define BIT_CLEAR(b, i) ((b)[(i)/8] &= ~(1 << ((i)%8)))
#define BIT_TEST(b, i)  ((b)[(i)/8] &   (1 << ((i)%8)))

#define PMM_MIN_ADDR 0x100000ULL // 1MB

static uint8_t* bitmap = 0;
static size_t bitmap_size = 0;

static paddr_t memory_base = 0;
static size_t total_pages = 0;
static size_t used_pages = 0;

void pmm_init(BootInfo* boot)
{
    EFI_MEMORY_DESCRIPTOR* map = (EFI_MEMORY_DESCRIPTOR*)boot->MemoryMap;
    size_t entries = boot->MemoryMapSize / boot->MemoryDescriptorSize;

    paddr_t highest = 0;

    // 1. compute max address
    for (size_t i = 0; i < entries; i++) {
        EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)map + i * boot->MemoryDescriptorSize);

        if (desc->Type != EfiConventionalMemory) { continue; }

        uint64_t end = desc->PhysicalStart + desc->NumberOfPages * PAGE_SIZE;
        if (end > highest) { highest = end; }
    }

    total_pages = highest / PAGE_SIZE;
    bitmap_size = (total_pages + 7) / 8; // make sure divide doesn't fail

    // 2. place bitmap in HHDM (temporary simple placement)
    bitmap = (uint8_t*)phys_to_hhdm(0x100000); // TEMP safe region

    memset(bitmap, 0xFF, bitmap_size);

    // mark usable memory as free
    for (size_t i = 0; i < entries; i++) {
        EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)map + i * boot->MemoryDescriptorSize);

        if (desc->Type == EfiConventionalMemory) {
            pmm_mark_free(desc->PhysicalStart, desc->NumberOfPages * PAGE_SIZE);
        }
    }

    paddr_t kstart = hhdm_to_phys((vaddr_t)__kernel_start);
    paddr_t kend   = hhdm_to_phys((vaddr_t)__kernel_end);

    // align to pages
    kstart &= ~(PAGE_SIZE - 1);
    kend = (kend + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    // mark kernel used
    pmm_mark_used(kstart, kend - kstart); 

    // mark EFI memory map buffer used
    pmm_mark_used(boot->MemoryMap, boot->MemoryMapSize);

    // reserve bitmap itself
    pmm_mark_used(hhdm_to_phys((vaddr_t)bitmap), bitmap_size);
}

paddr_t pmm_alloc_page(void)
{
    for (size_t i = 0; i < total_pages; i++) {
        if (!BIT_TEST(bitmap, i) && (i * PAGE_SIZE >= PMM_MIN_ADDR)) {
            BIT_SET(bitmap, i);
            used_pages++;
            return i * PAGE_SIZE;
        }
    }

    return 0; // out of memory
}

void pmm_free_page(paddr_t page)
{
    size_t index = page / PAGE_SIZE;

    if (BIT_TEST(bitmap, index)) {
        BIT_CLEAR(bitmap, index);
        used_pages--;
    }
}

paddr_t pmm_alloc_contiguous_pages(size_t count)
{
    size_t run = 0;
    size_t start = 0;

    for (size_t i = PMM_MIN_ADDR / PAGE_SIZE; i < total_pages; i++) {
        if (!BIT_TEST(bitmap, i)) {
            if (run == 0) {
                start = i;
            }

            run++;

            if (run == count) {
                for (size_t j = start; j < start + count; j++) {
                    BIT_SET(bitmap, j);
                    used_pages++;
                }

                return start * PAGE_SIZE;
            }
        } else {
            run = 0;
        }
    }

    return 0;
}

void pmm_free_countiguous_pages(paddr_t addr, size_t count)
{
    size_t start = addr / PAGE_SIZE;

    for (size_t i = start; i < start + count; i++) {
        if (BIT_TEST(bitmap, i)) {
            BIT_CLEAR(bitmap, i);
            used_pages--;
        }
    }
}

void pmm_mark_used(paddr_t start, size_t size)
{
    paddr_t end = start + size;

    size_t start_page = start / PAGE_SIZE;
    size_t end_page = (end + PAGE_SIZE - 1) / PAGE_SIZE;

    for (size_t i = start_page; i < end_page; i++) {
        if (!BIT_TEST(bitmap, i)) {
            BIT_SET(bitmap, i);
            used_pages++;
        }
    }
}

void pmm_mark_free(paddr_t start, size_t size)
{
    paddr_t end = start + size;

    size_t start_page = start / PAGE_SIZE;
    size_t end_page = (end + PAGE_SIZE - 1) / PAGE_SIZE;

    for (size_t i = start_page; i < end_page; i++) {
        if (BIT_TEST(bitmap, i)) {
            BIT_CLEAR(bitmap, i);
            if (used_pages > 0) { used_pages--; }
        }
    }
}

size_t pmm_total_pages(void) { return total_pages; }
size_t pmm_used_pages(void) { return used_pages; }