#pragma once

#include <windowing/window/window.h>
#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <de/surface.h>

void taskbar_init();

void taskbar_render(xenon_surface_t* s);
void taskbar_render_top_level(xenon_surface_t* s);

b8 taskbar_handle_click(xenon_surface_t* s, i32 mouse_x, i32 mouse_y);
void taskbar_process_mouse_events(xenon_surface_t* s, i32 mouse_x, i32 mouse_y);

b8 taskbar_create_icon_for_window(window_t* window, const char* path);
void taskbar_remove_icon_for_window(window_t* window);
void taskbar_set_active(window_t* window);