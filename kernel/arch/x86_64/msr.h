#pragma once
#include <xlibc/xstdint.h>
#include <kernel.h>

#define IA32_EFER       0xC0000080
#define IA32_EFER_SCE   0x00000001
#define IA32_STAR       0xC0000081
#define IA32_LSTAR      0xC0000082
#define IA32_FMASK      0xC0000084

#define IA32_GS_BASE        0xC0000101
#define IA32_KERNEL_GS_BASE 0xC0000102

#define IA32_THERM_STATUS  0x19C        // Intel temperature MSR
#define AMD_THERMTRIP      0xC0010042   // AMD temperature MSR

__PRIVILEGED_CODE extern ASMCALL u64 msr_read(u32 msr);
__PRIVILEGED_CODE extern ASMCALL void msr_write(u32 msr, u64 value);

// NOTE: returns degrees in celsius
__PRIVILEGED_CODE extern ASMCALL i32 msr_read_cpu_temperature();