#include "graphics.h"
#include <xlibc/stdlib.h>
#include <xlibc/xassert.h>
#include <memory/vmm.h>
#include <arch/x86_64/io.h>
#include <graphics/font8x8.h>
#include <graphics/memops.h>

#define FRAMEBUFFER_VIRT_BASE 0xFFFFD00000000000ULL

static uint32_t dirty_x1 = UINT32_MAX;
static uint32_t dirty_y1 = UINT32_MAX;
static uint32_t dirty_x2 = 0;
static uint32_t dirty_y2 = 0;

static uint32_t* backbuffer;
static uint32_t current_width, current_height, pixels_per_line, pixel_format;
const static uint32_t bytes_per_pixel = 4;
static paddr_t framebuffer_phys;

static inline void mark_dirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (x < dirty_x1) dirty_x1 = x;
    if (y < dirty_y1) dirty_y1 = y;
    if (x + w > dirty_x2) dirty_x2 = x + w;
    if (y + h > dirty_y2) dirty_y2 = y + h;
}

void graphics_init(Framebuffer* fb) {
    current_width = fb->Width;
    current_height = fb->Height;
    pixels_per_line = fb->PixelsPerScanLine;
    pixel_format = fb->PixelFormat;
    framebuffer_phys = (paddr_t)fb->BaseAddress;

    size_t framebuffer_size = pixels_per_line * current_height * bytes_per_pixel;
    vmm_map_mmio(&kernel_space, FRAMEBUFFER_VIRT_BASE, framebuffer_phys, framebuffer_size);

    // TODO: Fix heap to be able to handle this
    // backbuffer = (volatile u32*)FRAMBUFFER_VIRT_BASE;

    backbuffer = (uint32_t*)kmalloc(framebuffer_size);
    if (!backbuffer) {
        serial_write_str("Failed to allocate backbuffer!");
        while(1);
    }
}

void graphics_shutdown() {
    current_width = 0;
    current_height = 0;
    pixels_per_line = 0;
    pixel_format = 0;
    framebuffer_phys = 0;
    // TODO: Unmap MMIO
    if (backbuffer) { kfree(backbuffer); }
}

void graphics_swap_buffers() {
    if (dirty_x1 >= dirty_x2 || dirty_y1 >= dirty_y2) return;

    for (uint32_t y = dirty_y1; y < dirty_y2; y++) {
        uint32_t* src = backbuffer + y * pixels_per_line + dirty_x1;
        uint32_t* dst = (uint32_t*)FRAMEBUFFER_VIRT_BASE + y * pixels_per_line + dirty_x1;

        uint32_t width = dirty_x2 - dirty_x1;
        memcpy_fast_qword(dst, src, width / 2);

        if (width & 1)
            dst[width - 1] = src[width - 1];
    }

    // reset
    dirty_x1 = UINT32_MAX;
    dirty_y1 = UINT32_MAX;
    dirty_x2 = 0;
    dirty_y2 = 0;
}

uint32_t graphics_convert_color(uint8_t r, uint8_t g, uint8_t b) {
    if (pixel_format == 1) { // RGB
        return (r << 16) | (g << 8) | b;
    } else if (pixel_format == 0) { // BGR
        return (b << 16) | (g << 8) | r;
    } else {
        return (r << 16) | (g << 8) | b; // fallback
    }
}

void graphics_clear_screen(uint32_t color) {
    for (uint64_t i = 0; i < (uint64_t)pixels_per_line * current_height; i++) {
        backbuffer[i] = color;
    }
    mark_dirty(0, 0, current_width, current_height);
}

static inline void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= current_width || y >= current_height) {
        return;
    }
    // ((volatile uint32_t*)backbuffer)[y * pixels_per_line + x] = color;
    backbuffer[y * pixels_per_line + x] = color; // regular RAM buffer doesn't need volatile
}

void graphics_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t j = 0; j < h; j++) {
        uint32_t* row = (uint32_t*)backbuffer + (y + j) * pixels_per_line + x;
        uint64_t packed = ((uint64_t)color << 32) | color;
        memset_fast_qword((void*)row, packed, w / 2);
        if (w & 1) row[w - 1] = color;
    }
    mark_dirty(x, y, w, h);
}

void graphics_draw_char(char c, uint32_t x, uint32_t y, uint32_t color) {
    if (c < 0 || c > 127) return; // unsupported
    if (x + 8 > current_width || y + 8 > current_height) {
        return;
    }
    const uint8_t* glyph = font8x8_basic[(uint8_t)c];  // no -32
    for (uint32_t row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        uint32_t* row_ptr = backbuffer + (y + row) * pixels_per_line + x;

        for (uint32_t col1 = 0; col1 < 8; col1++) {
            if (bits & (1 << col1)) {
                row_ptr[col1] = color;
            }
        }
    }
    mark_dirty(x, y, 8, 8);
}

void graphics_draw_string(const char* str, uint32_t x, uint32_t y, uint32_t color) {
    uint32_t orig_x = x;
    while (*str) {
        if (*str == '\n') {
            y += 8;
            x = orig_x;
        } else {
            graphics_draw_char(*str, x, y, color);
            x += 8;
        }
        str++;
    }
}

void graphics_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    put_pixel(x, y, color);
    mark_dirty(x, y, 1, 1);
}

void graphics_draw_hex(uint64_t val, uint32_t x, uint32_t y, uint32_t color) {
    const char* hex = "0123456789ABCDEF";

    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (val >> i) & 0xF;
        graphics_draw_char(hex[nibble], x, y, color);
    }
}

void graphics_draw_decimal(uint64_t val, uint32_t x, uint32_t y, uint32_t color) {
    char buf[21]; // max for uint64_t = 20 digits + null
    int i = 0;

    // Special case: 0
    if (val == 0) {
        graphics_draw_char('0', x, y, color);
        return;
    }

    // Build digits in reverse
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }

    // Print in correct order
    while (i > 0) {
        graphics_draw_char(buf[--i], x, y, color);
    }
}

u32 graphics_get_framebuffer_width() {
    return current_width;
}

u32 graphics_get_framebuffer_height() {
    return current_height;
}

u32 graphics_get_framebuffer_stride() {
    return pixels_per_line;
}

u32 graphics_get_framebuffer_format() {
    return pixel_format;
}