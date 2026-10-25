#pragma once

#include <xlibc/xstdint.h>
#include <kernel.h>

#define SMP_MAX_CPUS 16

// a wake request cannot be recalled, so a CPU that misses its boot deadline
// may still start later. CLAIMED and ABANDONED are the two outcomes of the race
// between the AP taking ownership of its boot memory and the BSP giving up on it,
// decided by a single compare-exchange so exactly one side wins
#define SMP_CPU_OFFLINE     0
#define SMP_CPU_BOOTING     1 // wake sent, AP has not responded yet
#define SMP_CPU_ONLINE      2
#define SMP_CPU_CLAIMED     3 // AP owns its boot memory, still initializing
#define SMP_CPU_ABANDONED   4 // BSP gave up first, AP must not proceed

typedef struct {
    u32 logical_id; // 0-based index
    u64 hw_id; // APIC ID (x86)
    u32 state; // ***NOTE*** should use atomic operations, TODO: atomic struct. CPU_* above
    b8 is_bsp; // true for the bootstrap processor
} smp_cpu_info_t;

typedef enum {
    SMP_CPU_INIT_RESULT_OK = 0,
    SMP_CPU_INIT_RESULT_ERR_NO_CPUS = -1,
    SMP_CPU_INIT_RESULT_ERR_BOOT_TIMEOUT = -2,
    SMP_CPU_INIT_RESULT_ERR_PREPARE = -3
} smp_cpu_init_result_t;

/**
 * @brief Enumerate CPUs from ACPI MADT and initialize the SMP subsystem.
 * Marks the BSP as online, all APs as offline.
 * @note Call after acpi_init().
 * @returns SMP_CPU_INIT_RESULT_OK on success, otherwise a negative smp_cpu_init_result_t.
 */
__PRIVILEGED_CODE i32 smp_init();

/**
 * @brief Get number of enumerated CPUs (including BSP0).
 */
u32 smp_cpu_count();

/**
 * @brief Number of CPUs currently in CPU_ONLINE state.
 */
u32 smp_online_count();

/**
 * @brief Get CPU info by logical id.
 * @returns Returns NULL if out of range
 */
smp_cpu_info_t* smp_get_cpu_info(u32 logical_id);