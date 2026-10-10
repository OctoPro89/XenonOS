#pragma once

#include <xlibc/xstdint.h>
#include <de/surface.h>

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