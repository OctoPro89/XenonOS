#pragma once
#include <xlibc/xstdint.h>
#include <kernel.h>

extern __force_inline__ ASMCALL u64 __rdtsc__();

void ktimer_init();
void ktimer_calibrate_cpu_timer(u64 milliseconds);
void ktimer_start_cpu_periodic_timer(u8 irq_vector);

// cached time functions (not as precise, faster)
u64 ktimer_get_high_precision_system_time();
u64 ktimer_get_system_time_in_nanoseconds();
u64 ktimer_get_system_time_in_milliseconds();
u64 ktimer_get_system_time_in_seconds();

/**
 * @brief Gets absolute time with HPET.
 * @note ***IMPORTANT*** SLOW, use ktimer_get_fast_ns.
 * @returns nanoseconds
 */
u64 ktimer_get_unscheduled_ns();

/**
 * @brief Gets absolute time with HPET.
 * @note ***IMPORTANT*** SLOW, use ktimer_get_fast_ns.
 * @returns milliseconds
 */
u64 ktimer_get_unscheduled_ms();

/**
 * @brief Use TSC to read current time in nanoseconds, calibrated every tick by ktimer_sched_irq_global_tick.
 * @returns Current time in nanoseconds.
 */
u64 ktimer_get_fast_ns();

void ktimer_sched_irq_global_tick();

void sleep(u32 seconds);
void msleep(u32 milliseconds);
void usleep(u32 microseconds);
void nsleep(u32 nanoseconds);