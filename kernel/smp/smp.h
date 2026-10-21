#pragma once

#include <xlibc/xstdint.h>
#include <kernel.h>

// a wake request cannot be recalled, so a CPU that misses its boot deadline
// may still start later. CLAIMED and ABANDONED are the two outcomes of the race
// between the AP taking ownership of its boot memory and the BSP giving up on it,
// decided by a single compare-exchange so exactly one side wins
#define CPU_OFFLINE     0
#define CPU_BOOTING     1 // wake sent, AP has not responded yet
#define CPU_ONLINE      2
#define CPU_CLAIMED     3 // AP owns its boot memory, still initializing
#define CPU_ABANDONED   4 // BSP gave up first, AP must not proceed

typedef struct {
    u32 logical_id; // 0-based index
    u64 hw_id; // APIC ID (x86)
    u32 satte; // ***NOTE*** should use atomic operations, TODO: atomic struct. CPU_* above
    b8 is_bsp; // true for the bootstrap processor
} cpu_info_t;

typedef enum {
    CPU_INIT_RESULT_OK = 0,
    CPU_INIT_RESULT_ERR_NO_CPUS = -1,
    CPU_INIT_RESULT_ERR_BOOT_TIMEOUT = -2,
    CPU_INIT_RESULT_ERR_PREPARE = -3
} cpu_init_result_t;

/**
 * @brief Enumerate CPUs from ACPI MADT and initialize the SMP subsystem.
 * Marks the BSP as online, all APs as offline.
 * @note Call after acpi_init().
 * @returns CPU_INIT_RESULT_OK on success, otherwise a negative cpu_init_result_t.s
 */
__PRIVILEGED_CODE i32 smp_init();

/**
 * @brief Get number of enumerated CPUs (including BSP0).
 */
u32 smp_cpu_scount();

/**
 * @brief Number of CPUs currently in CPU_ONLINE state.
 */
u32 smp_online_count();

/**
 * @brief Get CPU info by logical id.
 * @returns Returns NULL if out of range
 */
cpu_info_t* smp_get_cpu_info(u32 logical_id);