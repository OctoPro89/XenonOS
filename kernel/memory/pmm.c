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

static PHYSICAL_ADDRESS memory_base = 0;
static size_t total_pages = 0;
static size_t used_pages = 0;

void pmm_init(BootInfo* boot)
{
    EFI_MEMORY_DESCRIPTOR* map = (EFI_MEMORY_DESCRIPTOR*)boot->MemoryMap;
    size_t entries = boot->MemoryMapSize / boot->MemoryDescriptorSize;

    PHYSICAL_ADDRESS highest = 0;

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
    bitmap = (uint8_t*)phys_to_virt(0x100000); // TEMP safe region

    memset(bitmap, 0xFF, bitmap_size);

    // mark usable memory as free
    for (size_t i = 0; i < entries; i++) {
        EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)map + i * boot->MemoryDescriptorSize);

        if (desc->Type == EfiConventionalMemory) {
            pmm_mark_free(desc->PhysicalStart, desc->NumberOfPages * PAGE_SIZE);
        }
    }

    PHYSICAL_ADDRESS kstart = virt_to_phys((VIRTUAL_ADDRESS)__kernel_start);
    PHYSICAL_ADDRESS kend   = virt_to_phys((VIRTUAL_ADDRESS)__kernel_end);

    // align to pages
    kstart &= ~(PAGE_SIZE - 1);
    kend = (kend + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    // mark kernel used
    pmm_mark_used(kstart, kend - kstart); 

    // mark EFI memory map buffer used
    pmm_mark_used(boot->MemoryMap, boot->MemoryMapSize);

    // reserve bitmap itself
    pmm_mark_used(virt_to_phys((VIRTUAL_ADDRESS)bitmap), bitmap_size);
}

PHYSICAL_ADDRESS pmm_alloc_page(void)
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

void pmm_free_page(PHYSICAL_ADDRESS page)
{
    size_t index = page / PAGE_SIZE;

    if (BIT_TEST(bitmap, index)) {
        BIT_CLEAR(bitmap, index);
        used_pages--;
    }
}

void pmm_mark_used(PHYSICAL_ADDRESS start, size_t size)
{
    PHYSICAL_ADDRESS end = start + size;

    size_t start_page = start / PAGE_SIZE;
    size_t end_page = (end + PAGE_SIZE - 1) / PAGE_SIZE;

    for (size_t i = start_page; i < end_page; i++) {
        if (!BIT_TEST(bitmap, i)) {
            BIT_SET(bitmap, i);
            used_pages++;
        }
    }
}

void pmm_mark_free(PHYSICAL_ADDRESS start, size_t size)
{
    PHYSICAL_ADDRESS end = start + size;

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