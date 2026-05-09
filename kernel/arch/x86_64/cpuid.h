#pragma once
#include <xlibc/xstdint.h>
#include <kernel.h>

// Basic CPUID Information
#define CPUID_VENDOR_ID            0x00000000
#define CPUID_FEATURES             0x00000001
#define CPUID_CACHE_DESC           0x00000002
#define CPUID_SERIAL_NUMBER        0x00000003

// Extended CPUID Information
#define CPUID_EXTENDED_FEATURES    0x80000001
#define CPUID_BRAND_STRING_1       0x80000002
#define CPUID_BRAND_STRING_2       0x80000003
#define CPUID_BRAND_STRING_3       0x80000004
#define CPUID_CACHE_INFO           0x80000006

// Feature bits in EDX for CPUID with EAX=1
#define CPUID_FEAT_EDX_PAE         (1 << 6)
#define CPUID_FEAT_EDX_APIC        (1 << 9)
#define CPUID_FEAT_EDX_PGE         (1 << 13)
#define CPUID_FEAT_EDX_PAT         (1 << 16)

// Feature bits in ECX for CPUID with EAX=1
#define CPUID_FEAT_ECX_SSE3        (1 << 0)
#define CPUID_FEAT_ECX_VMX         (1 << 5)

// Feature bits in ECX for CPUID with EAX=7, ECX=0
#define CPUID_FEAT_ECX_FSGSBASE    (1 << 0)
#define CPUID_FEAT_ECX_LA57        (1 << 16)  // 5-level paging

// SSE (Streaming SIMD Extensions) Feature Bit
#define CPUID_EDX_SSE         0x02000000

// SSE2 (Streaming SIMD Extensions 2) Feature Bit
#define CPUID_EDX_SSE2        0x04000000

// SSE3 (Streaming SIMD Extensions 3) Feature Bit
#define CPUID_ECX_SSE3        0x00000001

// AVX (Advanced Vector Extensions) Feature Bit
#define CPUID_ECX_AVX         0x10000000

// FMA3 (Fused Multiply-Add 3) Feature Bit
#define CPUID_ECX_FMA         0x00001000

// char* vendor - must be at least 13 bytes, returns null terminated
__PRIVILEGED_CODE extern ASMCALL void cpuid_read_vendor_id(char* vendor);
__PRIVILEGED_CODE extern ASMCALL u32 cpuid_read_cpu_family();