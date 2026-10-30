#pragma once

#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>

typedef struct xenon_surface xenon_surface_t;
void taskbar_render(xenon_surface_t* s);
b8 taskbar_handle_click(xenon_surface_t* s, i32 x, i32 y);
void taskbar_process_mouse_events(xenon_surface_t* s, i32 x, i32 y);
void taskbar_render_top_level(xenon_surface_t* s);