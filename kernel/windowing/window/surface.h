#pragma once

#include <xlibc/xstdint.h>

/**
 * @note Matches EFI pixel format from bootloader
 */
typedef enum {
    WINDOW_PIXEL_FORMAT_RGBX8888,
    WINDOW_PIXEL_FORMAT_BGRX8888
} window_pixel_format_t;

/**
 * @note A client-drawable pixel buffer.
 * 
 * `stride` is measured in bytes, not pixels.
 * `pixels` is borrowed kernel memory FOR NOW
 */
typedef struct window_surface {
    u32* pixels;

    u32 width;
    u32 height;
    u32 stride;

    window_pixel_format_t format;
    u32 generation;
} window_surface_t;