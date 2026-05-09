#include <arch/x86_64/apic/apic_timer.h>
#include <arch/x86_64/apic/lapic.h>

apic_timer_t apic_timer_global;

__PRIVILEGED_CODE static void apic_timer_setup(apic_timer_t* timer, u32 mode, u8 irq_number, u32 divide_config, u32 interval_value) {
    timer->irqno = irq_number;
    timer->divide_config = divide_config;
    timer->interval_value = interval_value;

    // set the timer mode
    lapic_write(APIC_TIMER_REGISTER, mode | irq_number);

    // set the divide configuration value
    lapic_write(APIC_TIMER_DIVIDE_CONFIG, divide_config);
    
    // set the timer interval value
    lapic_write(APIC_TIMER_INITIAL_COUNT, 0);
}

__PRIVILEGED_CODE void apic_timer_setup_periodic(apic_timer_t* timer, u8 irq_number, u32 divide_config, u32 interval_value) {
    apic_timer_setup(timer, APIC_TIMER_PERIODIC_MODE, irq_number, divide_config, interval_value);
}

__PRIVILEGED_CODE void apic_timer_setup_one_shot(apic_timer_t* timer, u8 irq_number, u32 divide_config, u32 interval_value) {
    apic_timer_setup(timer, APIC_TIMER_ONE_SHOT_MODE, irq_number, divide_config, interval_value);
}

__PRIVILEGED_CODE void apic_timer_start(const apic_timer_t* timer) {
    lapic_write(APIC_TIMER_INITIAL_COUNT, timer->interval_value);
}

__PRIVILEGED_CODE u32 apic_timer_read_counter(const apic_timer_t* timer) {
    return lapic_read(APIC_CURRENT_COUNT);
}

__PRIVILEGED_CODE u32 apic_timer_stop(const apic_timer_t* timer) {
    u32 cnt = apic_timer_read_counter(timer);
    // to stop the timer, set the initial count to zero
    lapic_write(APIC_TIMER_INITIAL_COUNT, 0);
    // read the current count value
    return cnt;
}