#pragma once
#include <xlibc/xstdint.h>
#include <kernel.h>

#define IA32_APIC_BASE_MSR          0x1B
#define APIC_REGISTER_SPACE_SIZE    0x400

// the offset of the ICR (Interrupt Command Register) in the Local APIC
#define APIC_ICR_LO 0x300  
#define APIC_ICR_HI 0x310

#define APIC_LVT_TIMER    0x320  // timer interrupt
#define APIC_LVT_THERMAL  0x330  // thermal sensor interrupt
#define APIC_LVT_PERF     0x340  // performance monitoring interrupt
#define APIC_LVT_LINT0    0x350  // LINT0 interrupt
#define APIC_LVT_LINT1    0x360  // LINT1 interrupt
#define APIC_LVT_ERROR    0x370  // error interrupt

__PRIVILEGED_CODE void lapic_init();

__PRIVILEGED_CODE u32 lapic_read(u32 reg);
__PRIVILEGED_CODE void lapic_write(u32 reg, u32 value);
__PRIVILEGED_CODE void lapic_mask_irq(u32 lvtoff);
__PRIVILEGED_CODE void lapic_unmask_irq(u32 lvtoff);
__PRIVILEGED_CODE void lapic_complete_irq();
__PRIVILEGED_CODE void lapic_mask_timer_irq();
__PRIVILEGED_CODE void lapic_unmask_timer_irq();
__PRIVILEGED_CODE void lapic_send_init_ipi(u8 apic_id);
__PRIVILEGED_CODE void lapic_send_startup_ipi(u8 apic_id, u32 vector);
__PRIVILEGED_CODE void lapic_wait_for_icr_cmd_completion();
__PRIVILEGED_CODE void lapic_disable_legacy_pic();