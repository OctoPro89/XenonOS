#pragma once
#include "../../shared/gop.h"
#include <xlibc/xstdint.h>

void graphics_init(Framebuffer* fb);
void graphics_shutdown();
void graphics_swap_buffers();
uint32_t graphics_convert_color(uint8_t r, uint8_t g, uint8_t b);
void graphics_clear_screen(uint32_t color);
void graphics_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void graphics_draw_char(char c, uint32_t x, uint32_t y, uint32_t color);
void graphics_draw_string(const char* str, uint32_t x, uint32_t y, uint32_t color);
void graphics_put_pixel(uint32_t x, uint32_t y, uint32_t color);

void graphics_scroll(uint32_t pixels, uint32_t color);

u32 graphics_get_framebuffer_width();
u32 graphics_get_framebuffer_height();
u32 graphics_get_framebuffer_stride();
u32 graphics_get_framebuffer_format();