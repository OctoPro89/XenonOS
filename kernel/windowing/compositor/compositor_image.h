
#pragma once

#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <de/surface.h>

typedef struct compositor_image {
    u32 width;
    u32 height;
    u32* pixels; // 0xAARRGGBB
} compositor_image_t;

/**
 * @brief Loads an image from file at `path` into `out`
 * @returns True on success, false otherwise
 */
b8 compositor_image_load_file(const char* path, compositor_image_t* out);

/**
 * @brief Frees image data from `compositor_image_load_file` etc.
 */
void compositor_image_destroy(compositor_image_t* image);

/**
 * @brief Scales `src` to `width` and `height` and allocates memory for `out`
 * @note Must free memory for `src` and `out
 */
b8 compositor_image_scale_nearest(const compositor_image_t* src, u32 width, u32 height, compositor_image_t* out);

/**
 * @brief Renders `image` into `dst` starting at `x`,`y` with `zero_is_transparent` making completely zeroed colors not rendered
 */
void compositor_image_draw(xenon_surface_t* dst, const compositor_image_t* image, i32 x, i32 y, b8 zero_is_transparent);