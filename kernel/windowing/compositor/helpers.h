#pragma once

#include <kernel.h>
#include <xlibc/xstdint.h>
#include <de/surface.h>

static __hint_inline__ b8 point_in_rect(i32 px, i32 py, i32 x, i32 y, i32 width, i32 height) {
    return px >= x && py >= y && px < x + width && py < y + height;
}

static void round_rect(xenon_surface_t* s, i32 x, i32 y, i32 w, i32 h, i32 radius, u32 color) {
    if (w <= 0 || h <= 0) return;

    if (radius < 0) radius = 0;
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;

    for (i32 row = 0; row < h; row++) {
        i32 inset = 0, dy = 0;

        if (row < radius)
            dy = radius - row;
        else if (row >= h - radius)
            dy = row - (h - radius - 1);

        if (dy) {
            i32 dx2 = radius * radius - dy * dy;
            i32 dx = 0;

            while ((dx + 1) * (dx + 1) <= dx2) dx++;
            inset = radius - dx;
        }

        if (inset < 0) inset = 0;

        if (w > inset * 2) {
            xenon_surface_rect(s, (u32)(x + inset), (u32)(y + row), (u32)(w - inset * 2), 1, color);
        }
    }
}