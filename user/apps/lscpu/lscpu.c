#include <stdio.h>
#include <arch/x86_64/uapi_cpuid.h>
#include <arch/x86_64/cpuid.h>
#include <arch/x86_64/syscall.h>

ssize_t syscall_get_cpu_info(cpu_info_t* buf) {
    long ret;

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_GET_CPU_INFO),
          "D"(buf)
        : "rcx", "r11", "r10", "memory"
    );

    return (ssize_t)ret;
}

i32 syscall_get_cpu_temp_c() {
    long ret;

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_HWMON_GET_CPU_TEMP_MC)
        : "rcx", "r11", "r10", "memory"
    );

    return (i32)ret;
}

int main() {
    cpu_info_t info;
    
    ssize_t result = syscall_get_cpu_info(&info);
    if (result < 0) {
        printf("lscpu: Failed to fetch CPU info from kernel (%d).\n", (int)result);
        return -1;
    }

    printf("Architecture:          x86_64\n");
    printf("Vendor ID:             %s\n", info.vendor);
    printf("Model name:            %s\n", info.brand);
    printf("CPU Family:            %d\n", info.family);
    printf("Model:                 %d\n", info.model);
    printf("Stepping:              %d\n", info.stepping);
    
    printf("Flags:                 ");
    if (info.edx_features & CPUID_FEAT_EDX_PAE)   printf("pae ");
    if (info.edx_features & CPUID_FEAT_EDX_APIC)  printf("apic ");
    if (info.edx_features & CPUID_FEAT_EDX_PGE)   printf("pge ");
    if (info.edx_features & CPUID_FEAT_EDX_PAT)   printf("pat ");
    if (info.edx_features & CPUID_EDX_SSE)        printf("sse ");
    if (info.edx_features & CPUID_EDX_SSE2)       printf("sse2 ");
    
    if (info.ecx_features & CPUID_FEAT_ECX_SSE3)  printf("sse3 ");
    if (info.ecx_features & CPUID_FEAT_ECX_VMX)   printf("vmx ");
    if (info.ecx_features & CPUID_ECX_AVX)        printf("avx ");
    if (info.ecx_features & CPUID_ECX_FMA)        printf("fma ");
    
    if (info.ecx_leaf7 & CPUID_FEAT_ECX_FSGSBASE) printf("fsgsbase ");
    if (info.ecx_leaf7 & CPUID_FEAT_ECX_LA57)     printf("la57 ");
    printf("\n");

    i32 temp_mc = syscall_get_cpu_temp_c();

    i32 whole = temp_mc / 1000;
    i32 fraction = temp_mc % 1000;

    // keep the fractional part positive for negative temperatures
    if (fraction < 0) {
        fraction = -fraction;
    }

    printf("Temperature: %d.%d C\n", whole, fraction);

    return 0;
}