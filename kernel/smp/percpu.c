#include <smp/percpu.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/msr.h>
#include <xlibc/string.h>

DEFINE_PER_CPU(u32, percpu_cpu_id);

// TODO: possibly syscall scratch

DEFINE_PER_CPU_BASE(uintptr_t, percpu_offset);

#define CPU0_AREA_SIZE 0x2000 // 8KB
static uint8_t g_cpu0_area[CPU0_AREA_SIZE] __attribute__((aligned(0x1000)));

uintptr_t __per_cpu_offset[SMP_MAX_CPUS];

uintptr_t this_cpu_offset() {
    uintptr_t off;
    asm volatile("mov %%gs:0, %0" : "=r"(off) :: "memory");
    return off;
}

__PRIVILEGED_CODE percpu_result_t percpu_init_bsp() {
    if (percpu_size() > CPU0_AREA_SIZE) {
        serial_write_str("Per-CPU area too large!");
        return PERCPU_RESULT_ERR_SIZE_MISMATCH;
    }

    for (uintptr_t i = 0; i < CPU0_AREA_SIZE; i++) {
        g_cpu0_area[i] = 0;
    }

    const uintptr_t base = (uintptr_t)g_cpu0_area;
    const uintptr_t tmpl = (uintptr_t)__percpu_start;
    const uintptr_t delta = base - tmpl;

    const uintptr_t percpu_off_off = (uintptr_t)(&percpu_offset) - tmpl;
    if (percpu_off_off != 0) {
        serial_write_str("percpu_offset not at offset 0 (got ");
        serial_write_dec(percpu_off_off);
        serial_write_str(")\n");
        return PERCPU_RESULT_ERR_LAYOUT;
    }

    /*
    if ((uintptr_t)(&syscall_scratch0) - tmpl != SYSCALL_SCRATCH0_OFFSET || (uintptr_t)(&syscall_scratch1) - tmpl != SYSCALL_SCRATCH1_OFFSET) {
        serial_print_str("syscall scratch slots not at fixed offsets");
        return PERCPU_RESULT_ERR_LAYOUT;
    }
    */

    *(uintptr_t*)(base + percpu_off_off) = delta;

    __per_cpu_offset[0] = delta;

    // set GS_BASE to kernel percpu
    // TODO: check
    msr_write(IA32_GS_BASE, base);

    msr_write(IA32_KERNEL_GS_BASE, base);

    return PERCPU_RESULT_OK;
}

__PRIVILEGED_CODE percpu_result_t percpu_init_ap(u32 cpu_id, vaddr_t base_va) {
    const uintptr_t tmpl = (uintptr_t)(__percpu_start);

    const uintptr_t percpu_off_off = (uintptr_t)(&percpu_offset) - tmpl;
    if (percpu_off_off != 0) {
        return PERCPU_RESULT_ERR_LAYOUT;
    }

    const uintptr_t delta = base_va - tmpl;

    memcpy((void*)base_va, __percpu_start, percpu_size());

    *(uintptr_t*)(base_va) = delta;

    __per_cpu_offset[cpu_id] = delta;

    // TODO: check
    msr_write(IA32_GS_BASE, base_va);
    msr_write(IA32_KERNEL_GS_BASE, base_va);

    this_cpu(percpu_cpu_id, u32) = cpu_id;

    return PERCPU_RESULT_OK;
}