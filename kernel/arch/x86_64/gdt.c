#include <xlibc/xstdint.h>
#include <smp/percpu.h>
#include <kernel.h>
#include "gdt.h"

// 8-byte code/data descriptors
struct __packed__ GDTEntry {
    u16 limit_low;
    u16 base_low;
    u8 base_mid;
    u8 access;
    u8 flags;
    u8 base_high;
};

// 16-byte TSS descriptor
struct __packed__ GDTEntryTSS {
    u16 limit_low;
    u16 base_low;
    u8 base_mid;
    u8 access;
    u8 flags;
    u8 base_high;
    u32 base_upper;
    u32 reserved;
};

// GDTPtr
struct __packed__ GDTPtr {
    u16 limit;
    u64 base;
};

// TSS
struct __packed__ TSS {
    u32 reserved0;
    u64 rsp0;
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist[7];
    u64 reserved2;
    u16 reserved3;
    u16 io_map_base;
};

#define IST_STACK_SIZE 0x1000
#define BOOT_KERNEL_STACK_SIZE 0x4000

/*
#define KERNEL_CS 0x08
#define KERNEL_SS 0x10
*/
#define TSS_SEL   0x28
#define KERNEL_CS 0x08
#define KERNEL_SS 0x10
#define USER_CS   0x1B  // 0x18 descriptor, RPL 3
#define USER_SS   0x23  // 0x20 descriptor, RPL 3

#define GDT_NULL_IDX      0
#define GDT_KERNEL_CS_IDX 1
#define GDT_KERNEL_DS_IDX 2
// #define GDT_USER_DS_IDX   3
#define GDT_USER_DS_IDX   4
// #define GDT_USER_CS_IDX   4
#define GDT_USER_CS_IDX   3
#define GDT_TSS_IDX       5 // Spans indices 5-6 (16 bytes)
#define GDT_ENTRIES       7

static const u8 ACCESSED    = 0x01;
static const u8 RW          = 0x02;
static const u8 DC          = 0x04;
static const u8 EXEC        = 0x08;
static const u8 CODE_DATA   = 0x10;
static const u8 DPL_RING0   = 0x00;
static const u8 DPL_RING3   = 0x60;
static const u8 PRESENT     = 0x80;

static const u8 KERNEL_CODE = PRESENT | CODE_DATA | EXEC | RW;
static const u8 KERNEL_DATA = PRESENT | CODE_DATA | RW;
static const u8 USER_CODE   = PRESENT | DPL_RING3 | CODE_DATA | EXEC | RW;
static const u8 USER_DATA   = PRESENT | DPL_RING3 | CODE_DATA | RW;

static const u8 AVL         = 0x10; // Bit 4: Available for system use
static const u8 LONG_MODE   = 0x20; // Bit 5: L=1 for 64-bit code segment
static const u8 SIZE_32     = 0x40; // Bit 6: D/B=1 for 32-bit default size
static const u8 GRANULARITY = 0x80; // Bit 7: G=1 for 4KB granularity

// Common combinations
static const u8 CODE_64     = GRANULARITY | LONG_MODE; // 0xA0: 64-bit code
static const u8 DATA_64     = GRANULARITY | SIZE_32; // 0xC0: 64-bit data

static const u8 TSS_AVAILABLE = 0x09; // 64-bit TSS (Available)
static const u8 TSS_BUSY      = 0x0B; // 64-bit TSS (Busy)
static const u8 LDT           = 0x02; // LDT

__attribute__((aligned(16))) static u8 g_bsp_ist1_stack[IST_STACK_SIZE];
__attribute__((aligned(16))) static u8 g_bsp_ist2_stack[IST_STACK_SIZE];
__attribute__((aligned(16))) static u8 g_bsp_ist3_stack[IST_STACK_SIZE];
__attribute__((aligned(16))) static u8 g_bsp_kernel_stack[BOOT_KERNEL_STACK_SIZE];

DEFINE_PER_CPU(struct GDTEntry[GDT_ENTRIES], cpu_gdt);
DEFINE_PER_CPU(struct TSS, cpu_tss);
DEFINE_PER_CPU(struct GDTPtr, cpu_gdt_ptr);
DEFINE_PER_CPU(struct GDTEntryTSS, cpu_tss_descriptor);

extern void ASMCALL x86_64_gdt_flush(const struct GDTPtr* gdtr, u16 cs, u16 ds, u16 tss);

// GDT array: null, code, data
extern u64 kernel_stack_top;

static struct GDTEntry make_segment_descriptor(u8 access_byte, u8 flags_nibble) {
    return (struct GDTEntry) {
        .limit_low = 0xFFFF,
        .base_low = 0x0000,
        .base_mid = 0x00,
        .access = access_byte,
        .flags = (u8)((flags_nibble & 0xF0) | 0x0F),
        .base_high = 0x00
    };
}

static struct GDTEntryTSS make_system_descriptor(u64 base, u32 limit, u8 type) {
    return (struct GDTEntryTSS) {
        .limit_low = (u16)(limit & 0xFFFF),
        .base_low = (u16)(base & 0xFFFF),
        .base_mid = (u8)((base >> 16) & 0xFF),
        .access = (u8)(PRESENT | (type & 0x0F)),
        .flags = (u8)((limit >> 16) & 0x0F),
        .base_high = (u8)((base >> 24) & 0xFF),
        .base_upper = (u32)((base >> 32) & 0xFFFFFFFF),
        .reserved = 0
    };
}

b8 gdt_init_percpu(uintptr_t rsp0, uintptr_t ist1, uintptr_t ist2, uintptr_t ist3) {
    struct GDTEntry* gdt = percpu_this_cpu_ptr(cpu_gdt);
    struct TSS* tss_ref = percpu_this_cpu_ptr(&cpu_tss);
    struct GDTPtr* gdt_ptr_ref = percpu_this_cpu_ptr(&cpu_gdt_ptr);
    struct GDTEntryTSS* tss_desc = percpu_this_cpu_ptr(&cpu_tss_descriptor);

    gdt[GDT_NULL_IDX] = (struct GDTEntry){ 0, 0, 0, 0, 0, 0 };
    gdt[GDT_KERNEL_CS_IDX] = make_segment_descriptor(KERNEL_CODE, CODE_64);
    gdt[GDT_KERNEL_DS_IDX] = make_segment_descriptor(KERNEL_DATA, DATA_64);
    gdt[GDT_USER_DS_IDX] = make_segment_descriptor(USER_DATA, DATA_64);
    gdt[GDT_USER_CS_IDX] = make_segment_descriptor(USER_CODE, CODE_64);

    u8* tss_bytes = (u8*)(tss_ref);
    for (u64 i = 0; i < sizeof(struct TSS); i++) {
        tss_bytes[i] = 0;
    }

    tss_ref->rsp0 = rsp0;
    tss_ref->ist[0] = ist1;
    tss_ref->ist[1] = ist2;
    tss_ref->ist[2] = ist3;
    tss_ref->io_map_base = sizeof(struct TSS);

    u64 tss_base = (u64)(tss_ref);
    u32 tss_limit = sizeof(struct TSS) - 1;

    *tss_desc = make_system_descriptor(tss_base, tss_limit, TSS_AVAILABLE);

    u8* tss_dst = (u8*)(&gdt[GDT_TSS_IDX]);
    const u8* tss_src = (const u8*)(tss_desc);
    for (u64 i = 0; i < sizeof(struct GDTEntryTSS); i++) {
        tss_dst[i] = tss_src[i];
    }

    gdt_ptr_ref->limit = (sizeof(struct GDTEntry) * GDT_ENTRIES) - 1;
    gdt_ptr_ref->base = (u64)(gdt);

    return true;
}

b8 gdt_init_bsp() {
    return gdt_init_percpu(
        (uintptr_t)(&g_bsp_kernel_stack[BOOT_KERNEL_STACK_SIZE]),
        (uintptr_t)(&g_bsp_ist1_stack[IST_STACK_SIZE]),
        (uintptr_t)(&g_bsp_ist2_stack[IST_STACK_SIZE]),
        (uintptr_t)(&g_bsp_ist3_stack[IST_STACK_SIZE])
    );
}

void gdt_load() {
    x86_64_gdt_flush(percpu_this_cpu_ptr(&cpu_gdt_ptr), KERNEL_CS, KERNEL_SS, TSS_SEL);
}

/*
__attribute__((aligned(16))) static uint8_t ist_stack[4096];
struct TSS tss = {0};
struct {
    struct GDTEntry entries[5];
    struct GDTEntryTSS tss;
} __packed__ gdt_full;

#define gdt (gdt_full.entries)
#define gdt_tss (gdt_full.tss)
struct GDTPtr gdt_ptr;

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
*/