#pragma once

#include <xlibc/xstdint.h>
#include <kernel.h>

/**
 * @brief Power off the machine.
 * Tries every mechanism the platform offsers, so it returns only
 * when all of them are exhausted or none are available.
 * @return -1 if the machine fails to power off
 */
__PRIVILEGED_CODE i32 shutdown();

/**
 * @brief Reboot the machine.
 * Tries every mechanism the platform offers, so it returns only
 * when all of them are exhausted or none are available.
 * @return -1 if the machine fails to reboot.
 */
__PRIVILEGED_CODE i32 reboot();