#pragma once
#include <xlibc/xstdint.h>

#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_USER     (1ULL << 2)
#define PAGE_WRITE_THROUGH (1ULL << 3)
#define PAGE_CACHE_DISABLE (1ULL << 4)
#define PAGE_PS       (1ULL << 7)

#define PAGE_SIZE 0x1000ULL

typedef uint64_t pte_t;

static inline uint16_t pml4_index(uint64_t v) { return (v >> 39) & 0x1FF; }
static inline uint16_t pdpt_index(uint64_t v) { return (v >> 30) & 0x1FF; }
static inline uint16_t pd_index(uint64_t v)   { return (v >> 21) & 0x1FF; }
static inline uint16_t pt_index(uint64_t v)   { return (v >> 12) & 0x1FF; }