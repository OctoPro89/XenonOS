#pragma once

#include <xlibc/xstdint.h>

typedef struct {
    void* driver_data;

    /**
     * @brief Reads data from the assigned block device interface.
     * @returns True on success, false otherwise.
     */
    b8 (*read)(void* driver_data, uint64_t lba, uint32_t count, void* buffer);
    
    /**
     * @brief Writes data to the assigned block device interface.
     * @returns True on success, false otherwise.
     */
    b8 (*write)(void* driver_data, uint64_t lba, uint32_t count, const void* buffer);

    /**
     * @brief Flushes data to the assigned block device interface.
     * @returns True on success, false otherwise.
     */
    b8 (*flush)(void* driver_data);
} block_device;