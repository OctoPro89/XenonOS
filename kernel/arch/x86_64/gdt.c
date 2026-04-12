#include <xlibc/xstdint.h>
#include "gdt.h"

// 8-byte code/data descriptors
struct __attribute__((packed)) GDTEntry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t flags;
    uint8_t base_high;
};

// 16-byte TSS descriptor
struct __attribute__((packed)) GDTEntryTSS {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t flags;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
};

// GDTPtr
struct __attribute__((packed)) GDTPtr {
    uint16_t limit;
    uint64_t base;
};

// TSS
struct __attribute__((packed)) TSS {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
};

__attribute__((aligned(16))) static uint8_t ist_stack[4096];

struct TSS tss = {0};

// GDT array: null, code, data
struct {
    struct GDTEntry entries[5];
    struct GDTEntryTSS tss;
} __attribute__((packed)) gdt_full;

#define gdt (gdt_full.entries)
#define gdt_tss (gdt_full.tss)

struct GDTPtr gdt_ptr;

extern uint64_t kernel_stack_top;

void gdt_init(void) {
    // Null descriptor
    gdt[0] = (struct GDTEntry){0};

    // Kernel code segment
    gdt[1].access = 0x9A; // present, code, exec/read
    gdt[1].flags  = 0xA0; // 64-bit long mode 0x80 (G) | 0x20 (L)
    gdt[1].limit_low = gdt[1].base_low = gdt[1].base_mid = gdt[1].base_high = 0;

    // Kernel data segment
    gdt[2].access = 0x92; // present, data, read/write
    gdt[2].flags  = 0xC0;
    gdt[2].limit_low = gdt[2].base_low = gdt[2].base_mid = gdt[2].base_high = 0;

    // User code segment
    gdt[3].access = 0xFA; // present, ring 3, executable
    gdt[3].flags  = 0xA8;
    gdt[3].limit_low = gdt[3].base_low = gdt[3].base_mid = gdt[3].base_high = 0;

    // User data segment
    gdt[4].access = 0xF2; // present, ring 3, writable
    gdt[4].flags  = 0x40;
    gdt[4].limit_low = gdt[4].base_low = gdt[4].base_mid = gdt[4].base_high = 0;

    // TSS descriptor
    uint64_t tss_addr = (uint64_t)&tss;
    uint32_t tss_limit = sizeof(tss) - 1;
    gdt_tss.limit_low = tss_limit & 0xFFFF;
    gdt_tss.base_low  = tss_addr & 0xFFFF;
    gdt_tss.base_mid  = (tss_addr >> 16) & 0xFF;
    gdt_tss.access    = 0x89; // present + type 9
    gdt_tss.flags     = (tss_limit >> 16) & 0x0F;
    gdt_tss.base_high = (tss_addr >> 24) & 0xFF;
    gdt_tss.base_upper= (tss_addr >> 32) & 0xFFFFFFFF;
    gdt_tss.reserved  = 0;

    // IST stack
    tss.ist[0] = (uint64_t)(ist_stack + sizeof(ist_stack));
    tss.rsp0 = kernel_stack_top;
    tss.io_map_base = sizeof(struct TSS);

    // GDTPtr
    gdt_ptr.base  = (uint64_t)&gdt_full;
    gdt_ptr.limit = sizeof(gdt_full) - 1;

    asm volatile ("lgdt %0" : : "m"(gdt_ptr));

    // Load TSS (selector = 0x28)
    asm volatile ("ltr %0" : : "r"((uint16_t)0x28));

    // Far jump to reload CS and DS/SS properly
    asm volatile (
        "pushq $0x08\n\t"        // code segment selector
        "lea 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "movw $0x10, %%ax\n\t"   // load kernel data segment selector
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        :
        :
        : "rax", "memory"
    );
}