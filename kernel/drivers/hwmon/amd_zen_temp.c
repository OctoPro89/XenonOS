#include <drivers/hwmon/amd_zen_temp.h>
#include <drivers/hwmon/amd_smn.h>

#define AMD_SMN_TEMP_REG    0x00059800u
#define AMD_TEMP_SHIFT      21
#define AMD_TEMP_RANGE_SEL  (1u << 19)
#define AMD_TEMP_TJ_SEL     (3u << 16)

b8 amd_zen_decode_temperature(u32 reg, i32* millidegrees_c) {
    if (!millidegrees_c || reg == 0xFFFFFFFFu) {
        return false;
    }

    i32 temp = (i32)((reg >> AMD_TEMP_SHIFT) * 125u);

    if ((reg & AMD_TEMP_RANGE_SEL) || (reg & AMD_TEMP_TJ_SEL) == AMD_TEMP_TJ_SEL) {
        temp -= 49000;
    }

    *millidegrees_c = temp;
    return true;
}

b8 amd_zen_read_temperature(i32* millidegrees_c) {
    if (!millidegrees_c) {
        return 0;
    }

    u32 reg;

    if (!amd_smn_read(AMD_SMN_TEMP_REG, &reg)) {
        return 0;
    }

    return amd_zen_decode_temperature(reg, millidegrees_c);
}