#pragma once

#include <kernel.h>

#define SERIAL_COM1 0x3F8

// void ASMCALL x64_outb(unsigned short port, unsigned char byte);
void x64_outb(u16 port, u8 byte);
void serial_write_char(char c);
void serial_write_str(const char* s);
void serial_write_hex(uint64_t val);
void serial_write_dec(uint64_t val);