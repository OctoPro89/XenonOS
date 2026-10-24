#pragma once

#include <memory/paging.h>

struct phys_range {
    paddr_t start;
    paddr_t end;
};

/**
 * @brief Physical frames the AP bring-up needs at fixed addresses, kept out
 * of the allocator for the life of the system. An empty range means none.
 * x86_64: the real-mode trampoline and its startup data below 1 MB.
 */
struct phys_range arch_smp_fixed_boot_frames();

/**
 * @brief Enumerate CPUs from the parsed ACPI MADT.
 * Fills the cpu_info array with one entry per CPU, marking which is the BSP.
 * @param cpus Output array to populate.
 * @param max Maximum entries (array capacity).
 * @return Number of CPUs found (including BSP).
 */
__PRIVILEGED_CODE u32 arch_smp_enumerate(smp_cpu_info_t* cpus, u32 max);

/**
 * @brief One-time setup before booting any AP.
 * x86_64: identity-maps trampoline region, copies trampoline code, inits startup data.
 * @return SMP_CPU_INIT_RESULT_OK on success, negative error code on failure.
 */
__PRIVILEGED_CODE i32 arch_smp_prepare();

/**
 * @brief Boot a single AP. Allocates stack, fills startup data, sends wake
 * sequence, polls cpu.state for CPU_ONLINE.
 * x86_64: INIT-SIPI-SIPI via LAPIC ICR.
 * AArch64: PSCI CPU_ON with trampoline entry point.
 * @param cpu The cpu_info entry to boot. State must be CPU_BOOTING on entry.
 * @return SMP_CPU_INIT_RESULT_OK on success, SMP_CPU_INIT_RESULT_ERR_BOOT_TIMEOUT if AP did not come online.
 */
__PRIVILEGED_CODE i32 arch_smp_boot_cpu(smp_cpu_info_t* cpu);

/**
 * @brief Prepare the interrupt path that `smp_raise_ipi` uses, on the BSP.
 * x86_64: nothing, the IPI vector is routed by the trap entry.
 * @return SMP_IPI_RESULT_OK on success, negative error code on failure.
 */
__PRIVILEGED_CODE i32 arch_smp_ipi_init();

/**
 * @brief Per-AP counterpart of `smp_ipi_init`, for state banked per CPU.
 * @return SMP_IPI_RESULT_OK on success, negative error code on failure.
 */
__PRIVILEGED_CODE i32 arch_smp_ipi_init_ap();

/**
 * @brief Interrupt `target` on the IPI vector. The caller has already
 * recorded what the target must do, this only delivers the interrupt.
 * x86_64: fixed-delivery IPI through the LAPIC ICR to the target's APIC id.
 * AArch64: software-generated interrupt to the target's CPU interface.
 * @return SMP_IPI_RESULT_OK, or SMP_IPI_RESULT_ERR_UNREACHABLE when the interrupt
 *         controller cannot address the target.
 */
__PRIVILEGED_CODE i32 arch_smp_raise_ipi(const smp_cpu_info_t* target);