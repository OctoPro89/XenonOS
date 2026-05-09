#include <arch/x86_64/irq.h>

static irq_handler_t handlers[256];

void irq_register_handler(uint8_t vec, irq_handler_t fn) {
    handlers[vec] = fn;
}

void irq_dispatch(struct regs* r) {
    irq_handler_t h = handlers[r->int_no];

    if (h) {
        h(r);
    }
}