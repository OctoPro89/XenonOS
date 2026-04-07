#pragma once

#include <xlibc/xstdint.h>

typedef struct {
    void* driver_data;

    int (*read)(void* driver_data, uint64_t lba, uint32_t count, void* buffer);
    int (*write)(void* driver_data, uint64_t lba, uint32_t count, const void* buffer);
} block_device;