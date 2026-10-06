#pragma once

#include <xlibc/xstdint.h>

#define XENON_KERNEL

#define ASMCALL // Would be SystemV abi but there is no macro for it

#define __PRIVILEGED_CODE
#define __PRIVILEGED_DATA

#define __hint_inline__ inline
#define __force_inline__ __attribute__((always_inline))

#define __packed__ __attribute__((packed))

#define USER_CODE_START  0x0000000000400000ULL
#define USER_STACK_TOP   0x0000000000800000ULL
#define USER_STACK_SIZE  (4 * PAGE_SIZE) // TODO: may want to make this 8 * PAGE_SIZe