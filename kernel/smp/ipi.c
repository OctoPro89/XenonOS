#include <smp/ipi.h>
#include <smp/smp.h>
#include <smp/percpu.h>
#include <arch/arch_smp.h>
#include <arch/x86_64/sync/sync.h>

// each CPU's pending word sits on its own cache line so senders targeting different CPUs never contend with each other.
struct pending_word {
    u32 bits;
} __attribute__((aligned(64)));

__PRIVILEGED_DATA static struct pending_word g_pending[SMP_MAX_CPUS] = {};
__PRIVILEGED_DATA static smp_ipi_handler_t g_handlers[SMP_IPI_CPU_MAX_MESSAGES] = {};
__PRIVILEGED_DATA static u32 g_registered = 0;
__PRIVILEGED_DATA static spinlock_t g_registry_lock;

// a valid message is a single bit at or below the highest registered slot
__PRIVILEGED_CODE static b8 is_registered(struct message m) {
    if (m.bit == 0 || (m.bit & (m.bit - 1)) != 0) {
        return false;
    }

    // TODO: check
    u32 index = (u32)(__builtin_ctz(m.bit));
    return index < (u32)__atomic_load_n(&g_registered, __ATOMIC_ACQUIRE);
}

// The fence publishes the pending word before the interrupt can be seen.
// Addressability of a CPU never changes, so undoing the mark after a failed
// raise cannot drop a message that another sender could have delivered.
__PRIVILEGED_CODE static i32 deliver(smp_cpu_info_t* target, struct message m) {
    u32 pending = g_pending[target->logical_id].bits;
    // TODO: check
    __atomic_fetch_or(&pending, m.bit, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_RELEASE);

    i32 rc = arch_smp_raise_ipi(target);
    if (rc != SMP_IPI_RESULT_OK) {
        __atomic_fetch_and(&pending, ~m.bit, __ATOMIC_RELAXED);
    }

    return rc;
}

__PRIVILEGED_CODE i32 smp_ipi_init() {
    spin_lock_init(&g_registry_lock);
    return arch_smp_ipi_init();
}

__PRIVILEGED_CODE i32 smp_ipi_init_ap() {
    return arch_smp_ipi_init_ap();
}

__PRIVILEGED_CODE i32 smp_ipi_register_message(smp_ipi_handler_t fn, struct message* out) {
    if (!fn || !out) {
        return SMP_IPI_RESULT_ERR_INVALID;
    }

    u64 flags = 0;
    spin_lock_irqsave(&g_registry_lock, &flags);
    if (g_registered == SMP_IPI_CPU_MAX_MESSAGES) {
        return SMP_IPI_RESULT_ERR_FULL;
    }

    g_handlers[g_registered] = fn;
    out->bit = 1u << g_registered;
    __atomic_store_n(&g_registered, g_registered + 1, __ATOMIC_RELEASE);

    spin_unlock_irqrestore(&g_registry_lock, flags);

    return SMP_IPI_RESULT_OK;
}

__PRIVILEGED_CODE i32 smp_ipi_send(u32 cpu, struct message m) {
    if (!is_registered(m)) {
        return SMP_IPI_RESULT_ERR_INVALID;
    }

    smp_cpu_info_t* target = smp_get_cpu_info(cpu);
    if (!target || __atomic_load_n(&target->state, __ATOMIC_ACQUIRE) != SMP_CPU_ONLINE) {
        return SMP_IPI_RESULT_ERR_OFFLINE;
    }

    return deliver(target, m);
}

__PRIVILEGED_CODE u32 smp_ipi_send_all_but_self(struct message m) {
    if (!is_registered(m)) {
        return 0;
    }

    u32 self = percpu_current_cpu_id();
    u32 sent = 0;
    for (u32 cpu = 0; cpu < smp_cpu_count(); cpu++) {
        smp_cpu_info_t* target = smp_get_cpu_info(cpu);
        if (cpu == self || __atomic_load_n(&target->state, __ATOMIC_ACQUIRE) != SMP_CPU_ONLINE) {
            continue;
        }

        if (deliver(target, m) == SMP_IPI_RESULT_OK) {
            ++sent;
        }
    }

    return sent;
}

__PRIVILEGED_CODE void smp_ipi_dispatch() {
    u32 cpu = percpu_current_cpu_id();
    u32 bits = __atomic_exchange_n(&g_pending[cpu].bits, 0, __ATOMIC_ACQUIRE);

    while (bits != 0) {
        u32 index = (u32)__builtin_ctz(bits);
        bits &= bits - 1;
        g_handlers[index]();
    }
}