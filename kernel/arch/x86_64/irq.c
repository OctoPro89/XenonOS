#include <arch/x86_64/irq.h>
#include <arch/x86_64/apic/lapic.h>

static irq_handler_t handlers[256];
static void* user_datas[256];

void irq_register_handler(uint8_t vec, irq_handler_t fn, void* user_data) {
    handlers[vec] = fn;
    user_datas[vec] = user_data;
}

void irq_dispatch(struct regs* r) {
    irq_handler_t h = handlers[r->int_no];

    if (h) {
        h(r, user_datas[r->int_no]);
    }

    lapic_complete_irq(); // MUST CALL THIS!
}

static u8 next_vector = 0x40;

u8 irq_alloc_vector() {
    return next_vector++;
}