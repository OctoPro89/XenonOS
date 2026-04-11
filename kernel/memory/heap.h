#pragma once
#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>

// TODO: Usage enums etc

void* kmalloc(size_t size);
void kfree(void* ptr);