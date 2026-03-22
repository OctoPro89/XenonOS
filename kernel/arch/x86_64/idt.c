#include "idt.h"

#define IDT_SIZE 256

static struct IDTEntry idt[IDT_SIZE];

extern void* isr_stub_table[]; // from ASM

static void set_idt_entry(int vec, void* handler) {
    uint64_t addr = (uint64_t)handler;

    idt[vec].offset_low  = addr & 0xFFFF;
    idt[vec].selector    = 0x08;     // kernel code segment
    idt[vec].ist         = 0;
    idt[vec].type_attr   = 0x8E;     // interrupt gate
    idt[vec].offset_mid  = (addr >> 16) & 0xFFFF;
    idt[vec].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt[vec].zero        = 0;
}

void idt_init(void) {
    for (int i = 0; i < 256; i++) {
        set_idt_entry(i, isr_stub_table[i]);
    }

    struct IDTR idtr = {
        .limit = sizeof(idt) - 1,
        .base = (uint64_t)idt
    };

    asm volatile ("lidt %0" : : "m"(idtr));
}