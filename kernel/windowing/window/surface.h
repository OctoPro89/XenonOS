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

/**
 * @brief Renders `c` at `x`,`y` with a bitmap 8x8 font into the surface's `pixels` with `color`
 */
void window_surface_render_char(window_surface_t* s, char c, u32 x, u32 y, u32 color);

/**
 * @brief Renders `str` at `x`,`y` with a bitmap 8x8 font into the surface's `pixels` with `color`
 */
void window_surface_render_string(window_surface_t* s, const char* str, u32 x, u32 y, u32 color);

/**
 * @brief Renders a rect at `x`,`y` with width of `w` and height of `h` and color of `color` into the surface's `pixels` 
 */
void window_surface_render_rect(window_surface_t* s, u32 x, u32 y, u32 w, u32 h, u32 color);