#include <graphics/graphics.h>
#include <arch/x86_64/io.h>

static u32 cursor_x = 0;
static u32 cursor_y = 0;

static const u32 xoff = 100;
static const u32 yoff = 50;

static const u32 char_width = 8;
static const u32 char_height = 8;

void stdio_impl_putchr(char c) {
    serial_write_char(c);

    u32 width = graphics_get_framebuffer_width();
    u32 height = graphics_get_framebuffer_height();

    u32 chars_per_line = (width - xoff) / char_width;

    u32 lines = (height - yoff) / char_height;

    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else {
        graphics_draw_char(
            c,
            xoff + cursor_x * char_width,
            yoff + cursor_y * char_height,
            0xFFFFFFFF
        );

        cursor_x++;

        // automatically wrap at the right edge
        if (cursor_x >= chars_per_line) {
            cursor_x = 0;
            cursor_y++;
        }
    }

    // gone below the last visible row
    if (cursor_y >= lines) {
        cursor_y = lines - 1;
    }

    graphics_swap_buffers();
}
