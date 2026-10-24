#pragma once
#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>

b8 gdt_init_bsp();
b8 gdt_init_percpu(uintptr_t rsp0, uintptr_t ist1, uintptr_t ist2, uintptr_t ist3);

void gdt_load();