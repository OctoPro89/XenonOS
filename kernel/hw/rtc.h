#pragma once

#include <xlibc/xstdint.h>
#include <kernel.h>

/**
 * @brief Read the hardware RTC and cache the boot-time Unix epoch.
 * @note x86_64: reads CMOS RTC via port I/O
 * @return True on success, else false
 */
__PRIVILEGED_CODE b8 rtc_init();

/**
 * @brief Unix epoch in nanoseconds captured at boot.
 * @returns 0 if rtc_init() has not been called, otherwise boot Unix ns
 */
u64 rtc_boot_unix_ns();