#include <windowing/window/surface.h>
#include <graphics/font8x8.h>
#include <graphics/memops.h>

void window_surface_render_char(window_surface_t* s, char c, u32 x, u32 y, u32 color) {
    if (c < 0 || c > 127) return; // unsupported
    if (x + 8 > s->width || y + 8 > s->height) {
        return;
    }
    const uint8_t* glyph = font8x8_basic[(uint8_t)c];  // no -32
    for (uint32_t row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        uint32_t* row_ptr = s->pixels + (y + row) * s->width + x;

        for (uint32_t col1 = 0; col1 < 8; col1++) {
            if (bits & (1 << col1)) {
                row_ptr[col1] = color;
            }
        }
    }
}

void window_surface_render_string(window_surface_t* s, const char* str, u32 x, u32 y, u32 color) {
    uint32_t orig_x = x;
    while (*str) {
        if (*str == '\n') {
            y += 8;
            x = orig_x;
        } else {
            window_surface_render_char(s, *str, x, y, color);
            x += 8;
        }
        str++;
    }
}

void window_surface_render_rect(window_surface_t* s, u32 x, u32 y, u32 w, u32 h, u32 color) {
    for (uint32_t j = 0; j < h; j++) {
        uint32_t* row = (uint32_t*)s->pixels + (y + j) * s->width + x;
        uint64_t packed = ((uint64_t)color << 32) | color;
        memset_fast_qword((void*)row, packed, w / 2);
        if (w & 1) row[w - 1] = color;
    }
}