#pragma once
#include <stdint.h>

extern struct TSS tss;
extern struct GDTPtr gdt_ptr;
void gdt_init(void);