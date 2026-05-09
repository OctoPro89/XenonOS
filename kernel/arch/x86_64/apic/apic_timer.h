#pragma once
#include <xlibc/xstdint.h>
#include <kernel.h>

#define APIC_TIMER_REGISTER        0x320
#define APIC_TIMER_DIVIDE_CONFIG   0x3E0
#define APIC_TIMER_INITIAL_COUNT   0x380
#define APIC_CURRENT_COUNT         0x390

#define APIC_TIMER_ONE_SHOT_MODE   0x0
#define APIC_TIMER_PERIODIC_MODE   0x20000

typedef struct {
    u8 irqno;
    u32 divide_config;
    u32 interval_value;
} apic_timer_t;

__PRIVILEGED_CODE void apic_timer_setup_periodic(apic_timer_t* timer, u8 irq_number, u32 divide_config, u32 interval_value);
__PRIVILEGED_CODE void apic_timer_setup_one_shot(apic_timer_t* timer, u8 irq_number, u32 divide_config, u32 interval_value);
__PRIVILEGED_CODE void apic_timer_start(const apic_timer_t* timer);
__PRIVILEGED_CODE u32 apic_timer_read_counter(const apic_timer_t* timer);
__PRIVILEGED_CODE u32 apic_timer_stop(const apic_timer_t* timer);

extern apic_timer_t apic_timer_global;