#pragma once

#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>

b8 amd_zen_read_temperature(i32* millidegrees_c);
b8 amd_zen_decode_temperature(u32 reg, i32* millidegrees_c);