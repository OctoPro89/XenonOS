#include <smp/smp.h>
#include <arch/x86_64/io.h>
#include <arch/arch_smp.h>
#include <xlibc/xstddef.h>

static smp_cpu_info_t g_cpus[SMP_MAX_CPUS];
static u32 g_cpu_count = 0;
static b8 g_smp_initialized = false;

__PRIVILEGED_CODE i32 smp_init() {
    if (g_smp_initialized) { return SMP_CPU_INIT_RESULT_OK; }

    g_cpu_count = arch_smp_enumerate(g_cpus, SMP_MAX_CPUS);

    if (g_cpu_count == 0) {
        serial_write_str("SMP: No CPUs found in MADT");
        return SMP_CPU_INIT_RESULT_ERR_NO_CPUS;
    }
    
    u32 ap_count = 0;
    for (u32 i = 0; i < g_cpu_count; ++i) {
        if (g_cpus[i].is_bsp) {
            // TODO: check
            __atomic_store_n(&g_cpus[i].state, SMP_CPU_ONLINE, __ATOMIC_RELAXED);
        }
        else {
            // TODO: check
            __atomic_store_n(&g_cpus[i].state, SMP_CPU_OFFLINE, __ATOMIC_RELAXED);
            ++ap_count;
        }
    }

    serial_write_str("SMP: ");
    serial_write_dec(ap_count);
    serial_write_str(" CPUs discovered\n");

    if (ap_count == 0) {
        g_smp_initialized = true;
        return SMP_CPU_INIT_RESULT_OK;
    }

    i32 rc = arch_smp_prepare();
    if (rc != SMP_CPU_INIT_RESULT_OK) {
        serial_write_str("SMP: Prepare failed!");
        g_smp_initialized = true;
        return rc;
    }

    for (u32 i = 0; i < g_cpu_count; ++i) {
        if (g_cpus[i].is_bsp) continue;

        // TODO: check
        __atomic_store_n(&g_cpus[i].state, SMP_CPU_BOOTING, __ATOMIC_RELEASE);

        rc = arch_smp_boot_cpu(&g_cpus[i]);
        if (rc == SMP_CPU_INIT_RESULT_OK) {
            serial_write_str("SMP: CPU ");
            serial_write_dec(g_cpus[i].logical_id);
            serial_write_str(" online\n");
        }
        else {
            // TODO: check
            __atomic_store_n(&g_cpus[i].state, SMP_CPU_OFFLINE, __ATOMIC_RELEASE);
            serial_write_str("SMP: CPU ");
            serial_write_dec(g_cpus[i].logical_id);
            serial_write_str(" failed to start (hw_id 0x");
            serial_write_hex(g_cpus[i].hw_id);
            serial_write_str(")\n");
        }
    }

    serial_write_str("SMP: ");
    serial_write_dec(smp_online_count());
    serial_write_str("/");
    serial_write_dec(g_cpu_count);
    serial_write_str(" CPUs online\n");
    g_smp_initialized = true;

    return SMP_CPU_INIT_RESULT_OK;
}

u32 smp_cpu_count() {
    return g_cpu_count;
}

u32 smp_online_count() {
    uint32_t count = 0;
    for (uint32_t i = 0; i < g_cpu_count; i++) {
        // TODO: check
        if (__atomic_load_n(&g_cpus[i].state, __ATOMIC_ACQUIRE) == SMP_CPU_ONLINE) {
            ++count;
        }
    }
    return count;
}

smp_cpu_info_t* smp_get_cpu_info(u32 logical_id) {
    if (logical_id >= g_cpu_count) {
        return NULL;
    }

    return &g_cpus[logical_id];
}