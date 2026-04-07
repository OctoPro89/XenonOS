#pragma once
#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>

void* kmalloc(size_t size);
void kfree(void* ptr);