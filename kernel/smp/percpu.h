#pragma once

#include <memory/paging.h>
#include <smp/smp.h>
#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <kernel.h>

extern char __percpu_start[];
extern char __percpu_end[];

extern uintptr_t __per_cpu_offset[SMP_MAX_CPUS];

#define DEFINE_PER_CPU_BASE(type, name) \
    __attribute__((section(".bss.percpu..base"))) \
    __typeof__(type) name
#define DECLARE_PER_CPU_BASE(type, name) \
    extern __attribute__((section(".bss.percpu..base"))) \
    __typeof__(type) name

#define DEFINE_PER_CPU(type, name) \
    __attribute__((section(".bss.percpu"))) \
    __typeof__(type) name
#define DECLARE_PER_CPU(type, name) \
    extern __attribute__((section(".bss.percpu"))) \
    __typeof__(type) name

#define DEFINE_PER_CPU_CACHELINE_ALIGNED(type, name) \
    __attribute__((section(".bss.percpu..cacheline_aligned"), aligned(64))) \
    __typeof__(type) name

#define DECLARE_PER_CPU_CACHELINE_ALIGNED(type, name) \
    extern __attribute__((section(".bss.percpu..cacheline_aligned"), aligned(64))) \
    __typeof__(type) name

DECLARE_PER_CPU_BASE(uintptr_t, percpu_offset);
DECLARE_PER_CPU(u32, percpu_cpu_id);

static __hint_inline__ uintptr_t percpu_size() {
    return (uintptr_t)(__percpu_end) - (uintptr_t)(__percpu_start);
}

uintptr_t this_cpu_offset();

static __hint_inline__ void* percpu_this_cpu_ptr(void* var) {
    const uintptr_t off = this_cpu_offset();
    return (void*)((uintptr_t)var + off);
}

static __hint_inline__ void* per_cpu_ptr_on(void* var, u32 cpu_id) {
    const uintptr_t off = __per_cpu_offset[cpu_id];
    return (void*)((uintptr_t)var + off);
}

typedef enum {
    PERCPU_RESULT_OK = 0,
    PERCPU_RESULT_ERR_SIZE_MISMATCH = -1,
    PERCPU_RESULT_ERR_LAYOUT = -2
} percpu_result_t;

/**
 * @brief Initialize per-cpu data for the BSP
 */
__PRIVILEGED_CODE percpu_result_t percpu_init_bsp();

/**
 * @brief Initialize per-cpu data for an AP. Sets GS base, copies template, stores offset. Must be the AP's first call in ap_entry()
 * @param cpu_id Logical CPU ID
 * @param base_va Virtual address of the pre-allocated per-cpu area
 */
__PRIVILEGED_CODE percpu_result_t percpu_init_ap(u32 cpu_id, vaddr_t base_va);

#define this_cpu(var, T) (*((T*)percpu_this_cpu_ptr(&(var))))
#define per_cpu_on(var, cpu_id, T) (*((T*)(per_cpu_ptr_on(&(var), cpu_id))))

static __hint_inline__ u32 percpu_current_cpu_id() {
    return this_cpu(percpu_cpu_id, u32);
}