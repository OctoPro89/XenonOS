#include <arch/x86_64/msr.h>
#include <arch/x86_64/cpuid.h>
#include <xlibc/string.h>

__PRIVILEGED_CODE i32 msr_read_cpu_temperature() {
    char cpu_vendor[24] = "";
    cpuid_read_vendor_id(cpu_vendor);
    u64 msr_value = 0;

    // Intel CPU temperature reading (IA32_THERM_STATUS)
    if (strcmp(cpu_vendor, "GenuineIntel") == 0) {
        msr_value = msr_read(IA32_THERM_STATUS);
        
        // Check if temperature sensor is enabled (Bit 31 must be set)
        if (!(msr_value & (1ULL << 31))) {
            return -1; // MSR not available
        }

        uint8_t temp_readout = (msr_value >> 16) & 0x7F;
        int32_t tj_max = 100; // Default TjMax (may vary by model)
        return tj_max - temp_readout;
    }

    // AMD CPU temperature reading (AMD THERMTRIP MSR)
    if (strcmp(cpu_vendor, "AuthenticAMD") == 0) {
        uint32_t cpu_family = cpuid_read_cpu_family();

        // AMD temperature MSR is only available on Family 10h (0x10) and newer
        if (cpu_family < 0x10) {
            return -1; // Temperature MSR is not available on older AMD CPUs
        }

        msr_value = msr_read(AMD_THERMTRIP);

        uint8_t temp_readout = (msr_value >> 16) & 0x7F;
        return (i32)(temp_readout - 49); // AMD offset correction
    }

    return -1; // Unsupported CPU vendor
}