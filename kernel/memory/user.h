#pragma once

#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>

int copy_to_user(void* user_dst, void* kernel_src, size_t size);
int copy_from_user(void* kernel_dst, const void* user_src, size_t size);