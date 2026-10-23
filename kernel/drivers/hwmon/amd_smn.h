#pragma once

#include <xlibc/xstdint.h>

b8 amd_smn_init();
b8 amd_smn_read(u32 address, u32* value);

b8 amd_smn_initialized();