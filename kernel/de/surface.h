#pragma once

#include <xlibc/xstdint.h>

typedef struct {
    u32 width;
    u32 height;
    u32 stride;
} xenon_surface_t;

b8 xenon_surface_get(xenon_surface_t** surface);

void xenon_surface_rect(xenon_surface_t* surface, u32 x, u32 y, u32 width, u32 height, u32 color);
void xenon_surface_str(xenon_surface_t* surface, u32 x, u32 y, const char* str, u32 color);
void xenon_surface_image_data(xenon_surface_t* surface, u32 x, u32 y, const u32* data, u32 width, u32 height, b8 zero_is_transparent);

void xenon_surface_present(xenon_surface_t* surface);