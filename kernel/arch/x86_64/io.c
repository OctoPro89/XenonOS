#include "io.h"

void x64_outb(u16 port, u8 byte) {
    asm volatile ("outb %0, %1" : : "a"(byte), "Nd"(port));
}

void serial_write_char(char c) {
    x64_outb(SERIAL_COM1, c);
}

void serial_write_str(const char* s) {
    while (*s) {
        serial_write_char(*s++);
    }
}

void serial_write_hex(uint64_t val) {
    const char* hex = "0123456789ABCDEF";

    serial_write_str("0x");

    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (val >> i) & 0xF;
        serial_write_char(hex[nibble]);
    }
}

void serial_write_dec(uint64_t val) {
    char buf[21]; // max for uint64_t = 20 digits + null
    int i = 0;

    // Special case: 0
    if (val == 0) {
        serial_write_char('0');
        return;
    }

    // Build digits in reverse
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }

    // Print in correct order
    while (i > 0) {
        serial_write_char(buf[--i]);
    }
}
