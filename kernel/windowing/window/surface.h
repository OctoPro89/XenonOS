#pragma once

#include <xlibc/xstdint.h>
#include <windowing/window/windefs.h>

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

    u32 refcount; // TODO: may need to be marked volatile

    window_pixel_format_t format;
    window_surface_handle_t handle; // NOTE: use for destruction
} window_surface_t;

/**
 * @brief Renders `c` at `x`,`y` with a bitmap 8x8 font into the surface's `pixels` with `color`
 */
void window_surface_render_char(const window_surface_t* s, char c, u32 x, u32 y, u32 color);

/**
 * @brief Renders `str` at `x`,`y` with a bitmap 8x8 font into the surface's `pixels` with `color`
 */
void window_surface_render_string(const window_surface_t* s, const char* str, u32 x, u32 y, u32 color);

/**
 * @brief Renders a rect at `x`,`y` with width of `w` and height of `h` and color of `color` into the surface's `pixels` 
 */
void window_surface_render_rect(const window_surface_t* s, u32 x, u32 y, u32 w, u32 h, u32 color);