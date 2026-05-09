#include <graphics/graphics.h>
#include <arch/x86_64/io.h>

static u32 cursor_x = 0;
static u32 cursor_y = 0;
static u32 max_chars_per_line = 100;

void stdio_impl_putchr(char c) {
    static u32 xoff = 100;
    static u32 yoff = 100;
    static u32 spacingx = 8;
    static u32 spacingy = 10;

    serial_write_char(c);
    graphics_draw_char(c, xoff + (spacingx * cursor_x), yoff + (spacingy * cursor_y), 0xFFFFFFFF);
    graphics_swap_buffers();

    ++cursor_x;

    if (c == '\n') {
        cursor_x = 0;
        cursor_y += 1;
    }

    if (cursor_x > max_chars_per_line) {
        yoff += spacingy;
        cursor_x = 0;
    }
}