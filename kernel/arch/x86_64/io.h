#pragma once

#include <kernel.h>

#define SERIAL_COM1 0x3F8

extern void ASMCALL x64_outb(u16 port, u8 value);

extern u32 ASMCALL x64_inl(u16 port);
extern void ASMCALL x64_outl(u16 port, u32 value);

void serial_write_char(char c);
void serial_write_str(const char* s);
void serial_write_hex(uint64_t val);
void serial_write_dec(uint64_t val);