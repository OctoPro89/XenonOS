#pragma once
#include <stdint.h>

typedef struct {
    void* BaseAddress;
    uint64_t BufferSize;
    uint32_t Width;
    uint32_t Height;
    uint32_t PixelsPerScanLine;
    uint32_t PixelFormat;
} Framebuffer;