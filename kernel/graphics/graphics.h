#pragma once
#include "../../shared/gop.h"
#include <xstdint.h>

void graphics_init(Framebuffer* fb);
void graphics_swap_buffers(Framebuffer* fb);
uint32_t convert_color(uint8_t r, uint8_t g, uint8_t b);
void clear_screen(uint32_t color);
void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void draw_char(char c, uint32_t x, uint32_t y, uint32_t color);
void draw_string(const char* str, uint32_t x, uint32_t y, uint32_t color);