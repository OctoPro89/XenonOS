#include "hal.h"
#include "gdt.h"
#include "idt.h"

void x86_64_HAL_init(void) {
    gdt_init_bsp();
    gdt_load();
    idt_init();
    asm volatile("sti");
}