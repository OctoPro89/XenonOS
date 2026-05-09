#pragma once
#include <xlibc/xstdint.h>
#include <kernel.h>

extern __FORCEINLINE ASMCALL u64 __rdtsc__();

void ktimer_init();
void ktimer_calibrate_cpu_timer(u64 milliseconds);
void ktimer_start_cpu_periodic_timer();
u64 ktimer_get_high_precision_system_time();
u64 ktimer_get_system_time_in_nanoseconds();
u64 ktimer_get_system_time_in_milliseconds();
u64 ktimer_get_system_time_in_seconds();
void ktimer_sched_irq_global_tick();

void sleep(u32 seconds);
void msleep(u32 milliseconds);
void usleep(u32 microseconds);
void nanosleep(u32 nanoseconds);