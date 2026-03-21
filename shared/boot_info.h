#pragma once
#include "gop.h"

typedef struct __attribute__((packed)) {
    uint64_t MemoryMap;
    uint64_t MemoryMapSize;
    uint64_t MemoryDescriptorSize;

    Framebuffer fb;
} BootInfo;