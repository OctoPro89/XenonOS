#include <windowing/compositor/compositor_image.h>
#include <graphics/graphics.h>
#include <memory/heap.h>
#include <xlibc/string.h>
#include <graphics/ximage.h>
#include <kernel.h>

b8 compositor_image_load_file(const char* path, compositor_image_t* out) {
    if (!path || !out) { return false; }

    *out = (compositor_image_t){ 0 };

    int width = 0, height = 0, channels = 0;
    // unsigned char* rgba = ximage_load(path, &width, &height, &channels, 4);
    FILE* f = fopen(path, "rb");
    if (!f) { return false; }

    if (fseek(f, 0, SEEK_END) < 0) { return false; }

    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return false;
    }

    if (fseek(f, 0, SEEK_SET) < 0) {
        fclose(f);
        return false;
    }

    unsigned char* buffer = kmalloc(size);
    if (!buffer) {
        fclose(f);
        return false;
    }

    u32 bytes_read = fread(buffer, 1, (u32)size, f);

    fclose(f);

    if (bytes_read != (u32)size) {
        kfree(buffer);
        return false;
    }
        
    unsigned char *rgba = ximage_load_from_memory(buffer, size, &width, &height, &channels, 4);

    kfree(buffer);

    if (!rgba || width <= 0 || height <= 0) {
        ximage_image_free(rgba);
        return false;
    }

    size_t w = (size_t)width;
    size_t h = (size_t)height;

    if (w > (size_t)-1 / h / sizeof(u32)) {
        ximage_image_free(rgba);
        return false;
    }

    size_t count = w * h;
    u32* pixels = kmalloc(count * sizeof(u32));

    if (!pixels) {
        ximage_image_free(rgba);
        return false;
    }

    for (size_t i = 0; i < count; ++i) {
        const unsigned char* p = rgba + i * 4;
        pixels[i] = ((u32)p[3] << 24) | ((u32)p[0] << 16) | ((u32)p[1] << 8)  | ((u32)p[2]);
    }

    ximage_image_free(rgba);

    out->width = (u32)width;
    out->height = (u32)height;
    out->pixels = pixels;

    return true;
}

void compositor_image_destroy(compositor_image_t* image) {
    if (!image) { return; }

    kfree(image->pixels);
    image->pixels = NULL;
    image->width = 0;
    image->height = 0;
}

b8 compositor_image_scale_nearest(const compositor_image_t* src, u32 width, u32 height, compositor_image_t* out) {
    if (!src || !src->pixels || !src->width || !src->height || !width || !height || !out) { return false; }

    *out = (compositor_image_t){ 0 };

    size_t w = width;
    size_t h = height;

    if (w > (size_t)-1 / h / sizeof(u32)) {
        return false;
    }

    u32* pixels = kmalloc(w * h * sizeof(u32));
    if (!pixels) {
        return false;
    }

    for (u32 y = 0; y < height; ++y) {
        u32 sy = (u32)(((u64)y * src->height) / height);
        const u32* src_row = src->pixels + (size_t)sy * src->width;
        u32* dst_row = pixels + (size_t)y * width;

        for (u32 x = 0; x < width; ++x) {
            u32 sx = (u32)(((u64)x * src->width) / width);
            dst_row[x] = src_row[sx];
        }
    }

    out->width = width;
    out->height = height;
    out->pixels = pixels;

    return true;
}

static __hint_inline__ u32 image_lerp_u8(u32 a, u32 b, u32 fraction) {
    return (u32)(((u64)a * (65536u - fraction) + (u64)b * fraction + 32768u) >> 16);
}

static __hint_inline__ u32 image_bilinear_channel(u32 p00, u32 p10, u32 p01, u32 p11, u32 shift, u32 fx, u32 fy) {
    u32 a = (p00 >> shift) & 0xFF;
    u32 b = (p10 >> shift) & 0xFF;
    u32 c = (p01 >> shift) & 0xFF;
    u32 d = (p11 >> shift) & 0xFF;

    u32 top = image_lerp_u8(a, b, fx);
    u32 bottom = image_lerp_u8(c, d, fx);

    return image_lerp_u8(top, bottom, fy);
}

b8 compositor_image_scale_linear(const compositor_image_t* src, u32 width, u32 height, compositor_image_t* out) {
    if (!src || !src->pixels ||
        !src->width || !src->height ||
        !width || !height || !out) {
        return false;
    }

    *out = (compositor_image_t){ 0 };

    size_t w = width;
    size_t h = height;

    if (w > (size_t)-1 / h / sizeof(u32)) {
        return false;
    }

    u32* pixels = kmalloc(w * h * sizeof(u32));

    if (!pixels) {
        return false;
    }

    // pixel-center mapping, in signed 16.16 fixed point this avoids the half-pixel shift associated with mapping destination pixel corners directly to source pixel corners
    const i64 x_step = ((u64)src->width << 16) / width;
    const i64 y_step = ((u64)src->height << 16) / height;

    i64 y_fp = (y_step - 65536) / 2;

    const i64 max_x = ((i64)src->width - 1) << 16;
    const i64 max_y = ((i64)src->height - 1) << 16;

    for (u32 y = 0; y < height; ++y) {
        i64 cy = y_fp;

        if (cy < 0) cy = 0;
        if (cy > max_y) cy = max_y;

        u32 sy0 = (u32)(cy >> 16);
        u32 sy1 = sy0 + 1;

        if (sy1 >= src->height) {
            sy1 = src->height - 1;
        }

        u32 fy = (u32)cy & 0xFFFF;

        const u32* row0 = src->pixels + (size_t)sy0 * src->width;
        const u32* row1 = src->pixels + (size_t)sy1 * src->width;

        i64 x_fp = (x_step - 65536) / 2;

        u32* dst_row = pixels + (size_t)y * width;

        for (u32 x = 0; x < width; ++x) {
            i64 cx = x_fp;

            if (cx < 0) cx = 0;
            if (cx > max_x) cx = max_x;

            u32 sx0 = (u32)(cx >> 16);
            u32 sx1 = sx0 + 1;

            if (sx1 >= src->width) {
                sx1 = src->width - 1;
            }

            u32 fx = (u32)cx & 0xFFFF;

            u32 p00 = row0[sx0];
            u32 p10 = row0[sx1];
            u32 p01 = row1[sx0];
            u32 p11 = row1[sx1];

            // Interpolate alpha first.
            u32 a = image_bilinear_channel(p00, p10, p01, p11, 24, fx, fy);

            u32 r = 0, g = 0, b = 0;

            if (a != 0) {
                // premultiply each source color by its alpha
                u32 r00 = (((p00 >> 16) & 0xFF) * (p00 >> 24) + 127) / 255;
                u32 r10 = (((p10 >> 16) & 0xFF) * (p10 >> 24) + 127) / 255;
                u32 r01 = (((p01 >> 16) & 0xFF) * (p01 >> 24) + 127) / 255;
                u32 r11 = (((p11 >> 16) & 0xFF) * (p11 >> 24) + 127) / 255;

                u32 g00 = (((p00 >> 8) & 0xFF) * (p00 >> 24) + 127) / 255;
                u32 g10 = (((p10 >> 8) & 0xFF) * (p10 >> 24) + 127) / 255;
                u32 g01 = (((p01 >> 8) & 0xFF) * (p01 >> 24) + 127) / 255;
                u32 g11 = (((p11 >> 8) & 0xFF) * (p11 >> 24) + 127) / 255;

                u32 b00 = ((p00 & 0xFF) * (p00 >> 24) + 127) / 255;
                u32 b10 = ((p10 & 0xFF) * (p10 >> 24) + 127) / 255;
                u32 b01 = ((p01 & 0xFF) * (p01 >> 24) + 127) / 255;
                u32 b11 = ((p11 & 0xFF) * (p11 >> 24) + 127) / 255;

                r = image_bilinear_channel(r00, r10, r01, r11, 0, fx, fy);
                g = image_bilinear_channel(g00, g10, g01, g11, 0, fx, fy);
                b = image_bilinear_channel(b00, b10, b01, b11, 0, fx, fy);

                r = (r * 255 + a / 2) / a;
                g = (g * 255 + a / 2) / a;
                b = (b * 255 + a / 2) / a;

                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;
            }

            dst_row[x] = (a << 24) | (r << 16) | (g << 8) | b;

            x_fp += x_step;
        }

        y_fp += y_step;
    }

    out->width = width;
    out->height = height;
    out->pixels = pixels;

    return true;
}

void compositor_image_draw(xenon_surface_t* dst, const compositor_image_t* image, i32 x, i32 y, b8 zero_is_transparent) {
    if (!dst || !image || !image->pixels || !image->width || !image->height) {
        return;
    }

    // use 64-bit bounds so off-screen positions cannot overflow
    i64 left = x;
    i64 top = y;
    i64 right = left + image->width;
    i64 bottom = top + image->height;

    if (left < 0) { left = 0; }
    if (top < 0) { top = 0; }

    if (right > dst->width) { right = dst->width; }
    if (bottom > dst->height) { bottom = dst->height; }

    if (right <= left || bottom <= top) { return; }

    u32 src_x = (u32)(left - x);
    u32 src_y = (u32)(top - y);
    u32 draw_width = (u32)(right - left);
    u32 draw_height = (u32)(bottom - top);

    for (u32 row = 0; row < draw_height; ++row) {
        const u32* pixels = image->pixels + (size_t)(src_y + row) * image->width + src_x;
        xenon_surface_image_data(dst, (u32)left,  (u32)(top + row), pixels, draw_width, 1, zero_is_transparent);
    }
}