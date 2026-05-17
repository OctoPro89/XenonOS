#pragma once

#include <xlibc/xstdint.h>

#define XENON_KERNEL

#define ASMCALL // Would be SystemV abi but there is no macro for it

#define __PRIVILEGED_CODE
#define __PRIVILEGED_DATA

#define __hint_inline__ inline
#define __force_inline__ __attribute__((always_inline))

#define __packed__ __attribute__((packed))