#pragma once

#include <xlibc/xstdint.h>
#include <de/surface.h>
#include <windowing/window/window.h>

b8 compositor_init(xenon_surface_t* target);

/**
 * @brief Mark the display as needing composition
 */
void compositor_invalidate();

/**
 * @brief Process window damage and present if needed
 */
void compositor_render();

void compositor_process_events();

typedef struct window window_t;

const window_surface_t* compositor_create_surface_for_window(window_t* window);
void compositor_release_surface(window_surface_handle_t surface_handle);