#include "hal.h"
#include "gdt.h"
#include "idt.h"

void x86_64_HAL_init(void) {
    gdt_init();
    idt_init();
    asm volatile("sti");
}