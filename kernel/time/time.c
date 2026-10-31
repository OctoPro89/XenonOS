#include <time/time.h>
#include <acpi/hpet.h>
#include <arch/x86_64/apic/apic_timer.h>
#include <arch/x86_64/apic/lapic.h>
#include <arch/x86_64/irq.h>
#include <xlibc/xstddef.h>
#include <acpi/hpet.h>
#include <task.h>

// TODO: check
// TODO: may need a lock for half-written time data (seqlock)

struct {
    u64 base_ns; // system time at the last LAPIC interrupt
    u64 base_tsc; // tsc value at the last LAPIC
    u32 tsc_mult; // fixed-point multiplier: (1,000,000,000 << shift) / tsc_freq
    u32 tsc_shift; // bit shift factor (e.g. 32)
} timekeeper;

u64 hardware_frequency = 0;
u64 apic_ticks_calibrated_frequency = 0;
u64 tsc_ticks_calibrated_frequency = 0;
u64 global_system_time_ns = 0;
u64 configured_apic_interval_ms = 0;

void ktimer_init() {
    hardware_frequency = hpet_query_frequency(&hpet_global);

    timekeeper.base_ns = 0;
    timekeeper.base_tsc = __rdtsc__();
    timekeeper.tsc_mult = 0;
    timekeeper.tsc_shift = 32; 
}

// TODO: possible overflow with multiplier
void ktimer_calibrate_cpu_timer(u64 milliseconds) {
    // disable timer interrupts
    lapic_mask_timer_irq();

    apic_timer_setup_one_shot(&apic_timer_global, IRQ0, 1, 0xffffffff);

    // record strart time from hpet and apic
    u64 hpet_start = hpet_read_counter(&hpet_global);
    u64 rdtsc_start = __rdtsc__();

    apic_timer_start(&apic_timer_global);

    // Wait 1 second
    while (hpet_read_counter(&hpet_global) - hpet_start < hardware_frequency) {
        asm volatile ("nop");
    }

    // Stop the APIC timer and get the current count
    uint32_t apic_end = apic_timer_stop(&apic_timer_global);
    uint64_t rdtsc_end = __rdtsc__();

    // re-enable timer interrupts
    // lapic_unmask_timer_irq();

    // calculate the number of elapsed apic ticks, assuming apic timer counts down from the initial counts
    apic_ticks_calibrated_frequency = (((uint64_t)(0xffffffff - apic_end)) / 1000) * milliseconds;
    tsc_ticks_calibrated_frequency = rdtsc_end - rdtsc_start;
    configured_apic_interval_ms = milliseconds;

    timekeeper.tsc_shift = 32;
    // (1,000,000,000 << 32) / tsc_ticks_per_second
    timekeeper.tsc_mult = (u32)(((1000000000ULL) << timekeeper.tsc_shift) / tsc_ticks_calibrated_frequency);
    
    // Seed initial values
    timekeeper.base_tsc = __rdtsc__();
    timekeeper.base_ns = global_system_time_ns;
}

void ktimer_start_cpu_periodic_timer(u8 irq_vector) {
    apic_timer_setup_periodic(&apic_timer_global, irq_vector, 1, apic_ticks_calibrated_frequency);
    apic_timer_start(&apic_timer_global);
}

u64 ktimer_get_high_precision_system_time() {
    return hpet_read_counter(&hpet_global);
}

u64 ktimer_get_system_time_in_nanoseconds() {
    return global_system_time_ns;
}

u64 ktimer_get_system_time_in_milliseconds() {
    return global_system_time_ns / 1000000ULL;
}

u64 ktimer_get_system_time_in_seconds() {
    return global_system_time_ns / 1000000000ULL;
}

u64 ktimer_get_unscheduled_ns() {
    u64 ticks = ktimer_get_high_precision_system_time();
    if (hardware_frequency == 0) return 0;
    u64 ns = (ticks / hardware_frequency) * 1000000000ULL + ((ticks % hardware_frequency) * 1000000000ULL) / hardware_frequency;
    return ns;
}

u64 ktimer_get_unscheduled_ms() {
    u64 ticks = ktimer_get_high_precision_system_time();
    if (hardware_frequency == 0) return 0;
    u64 ms = (ticks / hardware_frequency) * 1000ULL + ((ticks % hardware_frequency) * 1000ULL) / hardware_frequency;
    return ms;
}

u64 ktimer_get_fast_ns() {
    u64 current_tsc = __rdtsc__();

    if (current_tsc < timekeeper.base_tsc) {
        return timekeeper.base_ns; // handle rare TSC drift / out-of-order anomaliess
    }

    // u64 delta_tsc = current_tsc - timekeeper.base_tsc;

    // fast math (delta * mult) >> shift instead of divisions
    // u64 delta_ns = (delta_tsc * (u64)timekeeper.tsc_mult) >> timekeeper.tsc_shift;
    u64 delta_tsc = current_tsc - timekeeper.base_tsc;

    __uint128_t product = (__uint128_t)delta_tsc * timekeeper.tsc_mult;

    u64 delta_ns = (u64)(product >> timekeeper.tsc_shift);

    return timekeeper.base_ns + delta_ns;
}

void ktimer_sched_irq_global_tick() {
    global_system_time_ns += configured_apic_interval_ms * 1000000ULL;

    timekeeper.base_ns = global_system_time_ns;
    timekeeper.base_tsc = __rdtsc__();

    static u64 last_hpet_ticks = 0;
    u64 current_hpet_ticks = ktimer_get_high_precision_system_time();

    // check for HPET wraparound
    if (current_hpet_ticks < last_hpet_ticks) {
        // discard sync
        return;
    }

    // perform sync if at least 1 second has passed
    if ((current_hpet_ticks - last_hpet_ticks) >= hardware_frequency) {
        if (hardware_frequency > 0) {
            // calculate HPET-based nanoseconds
            u64 hpet_ns = (current_hpet_ticks * 1000000000ULL) / hardware_frequency;
            global_system_time_ns = hpet_ns;
            
            // Re-sync timekeeper values with drift corrected value
            timekeeper.base_ns = global_system_time_ns;
            timekeeper.base_tsc = __rdtsc__();
        }
        last_hpet_ticks = current_hpet_ticks;
    }
}

void sleep(u32 seconds) {
    task_sleep_ms((u64)(seconds) * 1000ULL);
}

void msleep(u32 milliseconds) {
    task_sleep_ms((u64)milliseconds);
}

void usleep(u32 microseconds) {
    task_sleep_ns((u64)(microseconds) * 1000ULL);
}

void nsleep(u32 nanoseconds) {
    task_sleep_ns(nanoseconds);
}