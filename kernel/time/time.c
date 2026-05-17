#include <time/time.h>
#include <acpi/hpet.h>
#include <arch/x86_64/apic/apic_timer.h>
#include <arch/x86_64/apic/lapic.h>
#include <arch/x86_64/irq.h>
#include <xlibc/xstddef.h>
#include <acpi/hpet.h>

u64 hardware_frequency = 0;
u64 apic_ticks_calibrated_frequency = 0;
u64 tsc_ticks_calibrated_frequency = 0;
u64 global_system_time_ns = 0;
u64 configured_apic_interval_ms = 0;

void ktimer_init() {
    hardware_frequency = hpet_query_frequency(&hpet_global);
}

void ktimer_calibrate_cpu_timer(u64 milliseconds) {
    // disable timer interrupts
    lapic_mask_timer_irq();

    apic_timer_setup_one_shot(&apic_timer_global, IRQ0, 1, 0xffffffff);

    // record strart time from hpet and apic
    u64 hpet_start = hpet_read_counter(&hpet_global);
    u64 rdtsc_start = __rdtsc__();

    // Wait 1 second
    while (hpet_read_counter(&hpet_global) - hpet_start < hardware_frequency) {
        asm volatile ("nop");
    }

    // Stop the APIC timer and get the current count
    uint32_t apic_end = apic_timer_stop(&apic_timer_global);
    uint64_t rdtsc_end = __rdtsc__();

    // re-enable timer interrupts
    lapic_unmask_timer_irq();

    // calculate the number of elapsed apic ticks, assuming apic timer counts down from the initial counts
    apic_ticks_calibrated_frequency = (((uint64_t)(0xffffffff - apic_end)) / 1000) * milliseconds;
    tsc_ticks_calibrated_frequency = rdtsc_end - rdtsc_start;
    configured_apic_interval_ms = milliseconds;
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

void ktimer_sched_irq_global_tick() {
    global_system_time_ns += configured_apic_interval_ms * 1000000ULL;

    static u64 last_hpet_ticks = 0;
    u64 current_hpet_ticks = ktimer_get_high_precision_system_time();

    // check for HPET wraparound
    if (current_hpet_ticks < last_hpet_ticks) {
        // discard sync
        return;
    }

    // perform sync if at least 1 second has passed
    if ((current_hpet_ticks - last_hpet_ticks) >= hardware_frequency) {
        // Calculate HPET-based nanoseconds
        uint64_t hpet_ns = (current_hpet_ticks * 1000000000ULL) / hardware_frequency;

        // Synchronize global time
        global_system_time_ns = hpet_ns;
        last_hpet_ticks = current_hpet_ticks;
    }
}

void sleep(u32 seconds) {
    u64 start = hpet_read_counter(&hpet_global);
    u64 target = start + (seconds * hardware_frequency);

    while (true) {
        u64 current = hpet_read_counter(&hpet_global);
        if (current < start) { // wraparound detected
            start = current;
            target = start + (seconds * hardware_frequency);
        }
        if (current >= target) break;

        asm volatile("pause");
    }
}

void msleep(u32 milliseconds) {
    u64 start = hpet_read_counter(&hpet_global);
    u64 target = start + (milliseconds * (hardware_frequency / 1000ULL));

    while (true) {
        u64 current = hpet_read_counter(&hpet_global);
        if (current < start) { // wraparound detected
            start = current;
            target = start + (milliseconds * (hardware_frequency / 1000ULL));
        }
        if (current >= target) break;

        asm volatile("pause");
    }
}

void usleep(u32 microseconds) {
    u64 start = hpet_read_counter(&hpet_global);
    u64 target = start + (microseconds * (hardware_frequency / 1000000ULL));

    while (true) {
        u64 current = hpet_read_counter(&hpet_global);
        if (current < start) { // wraparound detected
            start = current;
            target = start + (microseconds * (hardware_frequency / 1000000ULL));
        }
        if (current >= target) break;

        asm volatile("pause");
    }
}

void nanosleep(u32 nanoseconds) {
    u64 start = hpet_read_counter(&hpet_global);
    u64 target = start + (nanoseconds * (hardware_frequency / 1000000000ULL));

    while (true) {
        u64 current = hpet_read_counter(&hpet_global);
        if (current < start) { // wraparound detected
            start = current;
            target = start + (nanoseconds * (hardware_frequency / 1000000000ULL));
        }
        if (current >= target) break;

        asm volatile("pause");
    }
}