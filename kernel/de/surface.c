#include <de/surface.h>
#include <graphics/graphics.h>
#include <xlibc/xstddef.h>

static xenon_surface_t crnt_surface;

b8 xenon_surface_get(xenon_surface_t** surface) {
    *surface = &crnt_surface;
    (*surface)->width = graphics_get_framebuffer_width();
    (*surface)->height = graphics_get_framebuffer_height();
    (*surface)->stride = graphics_get_framebuffer_stride();
    return true;
}

void xenon_surface_rect(xenon_surface_t* surface, u32 x, u32 y, u32 width, u32 height, u32 color) {
    graphics_draw_rect(x, y, width, height, color);
}

void xenon_surface_str(xenon_surface_t* surface, u32 x, u32 y, const char* str, u32 color) {
    graphics_draw_string(str, x, y, color);
}

void xenon_surface_image_data(xenon_surface_t* surface, u32 x, u32 y, const u32* data, u32 width, u32 height, b8 zero_is_transparent) {
    (void)surface;

    graphics_draw_image_data(x, y, data, width, height, zero_is_transparent);
}

void xenon_surface_present(xenon_surface_t* surface) {
    graphics_swap_buffers();
}