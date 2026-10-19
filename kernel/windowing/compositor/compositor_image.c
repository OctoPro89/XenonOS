#include <windowing/compositor/compositor_image.h>
#include <graphics/graphics.h>
#include <memory/heap.h>
#include <xlibc/string.h>
#include <graphics/ximage.h>

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