#pragma once
#include "../../shared/gop.h"

uint32_t convert_color(uint8_t r, uint8_t g, uint8_t b, Framebuffer* fb);
void clear_screen(Framebuffer* fb, uint32_t color);
void draw_rect(Framebuffer* fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void draw_char(Framebuffer* fb, char c, uint32_t x, uint32_t y, uint32_t color);
void draw_string(Framebuffer* fb, const char* str, uint32_t x, uint32_t y, uint32_t color);