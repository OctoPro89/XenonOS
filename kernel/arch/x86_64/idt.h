#pragma once
#include <xlibc/xstdint.h>

struct IDTEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __packed__;

struct IDTR {
    uint16_t limit;
    uint64_t base;
} __packed__;

void idt_init(void);