#include <windowing/compositor/compositor.h>
#include <windowing/window/window.h>
#include <windowing/window/internal.h>
#include <de/mouse_cursor.h>
#include <drivers/input/input.h>
#include <task.h>
#include <xlibc/string.h>
#include <graphics/graphics.h>
#include <kernel.h>

// IMPORTANT NOTE: shouldn't need any locking AS LONG AS this is all on one task

#define DESKTOP_BG    0xFF18212F
#define DESKTOP_PANEL 0xFF101722
#define WINDOW_BODY   0xFF252F40
#define WINDOW_TITLE  0xFF35445C
#define WINDOW_BORDER 0xFF64748B
#define TEXT_WHITE    0xFFFFFFFF
#define TEXT_MUTED    0xFFB8C5D6
#define ACCENT_BLUE   0xFF60A5FA
#define ACCENT_RED    0xFF5B5B5B

#define CURSOR_W MOUSE_CURSOR_FRAME_WIDTH
#define CURSOR_H MOUSE_CURSOR_FRAME_HEIGHT
#define TASKBAR_H 40
#define TITLEBAR_H 30
#define WINDOW_RADIUS 10
#define MOUSE_BUTTON_LEFT (1u << 0)

#define COMPOSITOR_MAX_WINDOWS WINDOW_MAX_COUNT

static xenon_surface_t* target;
static b8 frame_invalidated = true;

static i32 cursor_x, cursor_y;
static u32 previous_buttons;
static i32 dragging_id = -1;
static i32 drag_offset_x, drag_offset_y;

// stable per-frame copies of window meta
static window_snapshot_t snapshots[COMPOSITOR_MAX_WINDOWS];
static u32 snapshot_count;

static void clamp_cursor() {
    i32 max_x = target->width > CURSOR_W ? (i32)target->width - CURSOR_W : 0;
    i32 max_y = target->height > CURSOR_H ? (i32)target->height - CURSOR_H : 0;

    if (cursor_x < 0) { cursor_x = 0; }
    if (cursor_y < 0) { cursor_y = 0; }
    if (cursor_x > max_x) { cursor_x = max_x; }
    if (cursor_y > max_y) { cursor_y = max_y; }
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

// converts the supported client formats into opaque 0xXXRRGGBB pixels
static __hint_inline__ u32 convert_pixel(u32 pixel, window_pixel_format_t format) {
    if (format == graphics_get_framebuffer_format()) { return pixel; };
    
    u32 r, g, b;

    if (format == WINDOW_PIXEL_FORMAT_BGRX8888) {
        b = (pixel >> 16) & 0xFF;
        g = (pixel >> 8) & 0xFF;
        r = pixel & 0xFF;
    } else {
        r = (pixel >> 16) & 0xFF;
        g = (pixel >> 8) & 0xFF;
        b = pixel & 0xFF;
    }

    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

#define COMPOSITOR_MAX_ROW_PIXELS 4096
static u32 converted_row[COMPOSITOR_MAX_ROW_PIXELS];

/*
static void blit_window_surface(xenon_surface_t* dst, const window_snapshot_t* win) {
    window_surface_t* src = win->surface;
    if (!src || !src->pixels) { return; }

    // the client content begins below the title bar, clip against the target's right / bottom edges
    i32 x = (i32)win->rect.x + 1; // NOTE: + 1 helps with the border
    i32 y = (i32)win->rect.y + TITLEBAR_H;

    if (x < 0 || y < 0) { return; }
    if ((u32)x >= dst->width || (u32)y >= dst->height) { return; }

    u32 width = src->width;
    u32 height = src->height;

    u32 max_width = dst->width - (u32)x;
    u32 max_height = dst->height - (u32)y;

    if (width > max_width) width = max_width;
    if (height > max_height) height = max_height;
    if (width == 0 || height == 0) { return; }

    if (width > COMPOSITOR_MAX_ROW_PIXELS) {
        width = COMPOSITOR_MAX_ROW_PIXELS;
    }

    const u32 stride_pixels = src->stride / sizeof(u32);

    for (u32 row = 0; row < height; row++) {
        const u32* input = (const u32*)( (const u8*)src->pixels + row * src->stride);

        for (u32 col = 0; col < width; col++) {
            converted_row[col] = convert_pixel(input[col], src->format);
        }

        xenon_surface_image_data(dst, (u32)x, (u32)y + row, converted_row, width, 1, false);
    }

    (void)stride_pixels;
}
*/

// more-expensive rounded version
static void blit_window_surface(xenon_surface_t* dst, const window_snapshot_t* win) {
    window_surface_t* src = win->surface;
    if (!src || !src->pixels) {
        return;
    }

    i32 wx = (i32)win->rect.x;
    i32 wy = (i32)win->rect.y;
    i32 ww = (i32)win->rect.width;
    i32 wh = (i32)win->rect.height;

    // match inner body
    const i32 border = 1;

    i32 content_x = wx + border;
    i32 content_y = wy + TITLEBAR_H;
    i32 content_w = ww - (border * 2);
    i32 content_h = wh - TITLEBAR_H - border;

    if (content_w <= 0 || content_h <= 0) {
        return;
    }

    i32 width = (i32)src->width;
    i32 height = (i32)src->height;

    if (width > content_w) width = content_w;
    if (height > content_h) height = content_h;

    for (i32 row = 0; row < height; ++row) {
        i32 dy = content_y + row;

        if (dy < 0 || dy >= (i32)dst->height) {
            continue;
        }

        // the bottom corners are rounded (top corners are too but for titlebar), calculate inset for this row relative to the bottom of the window
        i32 inset = 0;
        i32 row_in_window = dy - wy;

        if (row_in_window >= wh - WINDOW_RADIUS) {
            i32 dy_circle = row_in_window - (wh - WINDOW_RADIUS - 1);

            i32 dx2 = WINDOW_RADIUS * WINDOW_RADIUS - dy_circle * dy_circle;
            i32 dx = 0;

            if (dx2 > 0) {
                while ((dx + 1) * (dx + 1) <= dx2) {
                    ++dx;
                }
            }

            inset = WINDOW_RADIUS - dx;
        }

        i32 left = content_x + inset;
        i32 right = content_x + width - inset;

        if (left < 0) left = 0;
        if (right > (i32)dst->width) right = (i32)dst->width;

        if (right <= left) {
            continue;
        }

        u32 src_x = (u32)(left - content_x);
        u32 copy_width = (u32)(right - left);

        const u32* input = (const u32*)((const u8*)src->pixels + (u32)row * src->stride);

        if (copy_width > COMPOSITOR_MAX_ROW_PIXELS) {
            copy_width = COMPOSITOR_MAX_ROW_PIXELS;
        }

        for (u32 col = 0; col < copy_width; ++col) {
            converted_row[col] = convert_pixel(input[src_x + col], src->format);
        }

        xenon_surface_image_data(dst, (u32)left, (u32)dy, converted_row, copy_width, 1, false);
    }
}

static void render_window( xenon_surface_t* s, const window_snapshot_t* win) {
    /*
    if (win->state == WINDOW_STATE_HIDDEN || win->state == WINDOW_STATE_MINIMIZED) {
        return;
    }
    */

    i32 x = (i32)win->rect.x;
    i32 y = (i32)win->rect.y;
    i32 w = (i32)win->rect.width;
    i32 h = (i32)win->rect.height;

    if (w < 4 || h < 4) { return; }

    round_rect(s, x, y, w, h, WINDOW_RADIUS, WINDOW_BORDER);
    round_rect(s, x + 1, y + 1, w - 2, h - 2, WINDOW_RADIUS - 1, WINDOW_BODY);
    round_rect(s, x + 1, y + 1, w - 2, TITLEBAR_H, WINDOW_RADIUS - 1, WINDOW_TITLE);

    if (TITLEBAR_H > WINDOW_RADIUS) {
        xenon_surface_rect(s, (u32)(x + 1), (u32)(y + WINDOW_RADIUS), (u32)(w - 2), TITLEBAR_H - WINDOW_RADIUS + 1, WINDOW_TITLE);
    }

    xenon_surface_str(s, x + 10, y + 9, win->title, TEXT_WHITE);

    xenon_surface_rect(s, x + w - 23, y + 9, 10, 10, ACCENT_RED);
    xenon_surface_rect(s, x + w - 41, y + 9, 10, 10, ACCENT_BLUE);

    // client pixels are drawn over the body under decorations
    blit_window_surface(s, win);
}

static void render_desktop_background() {
    xenon_surface_rect(target, 0, 0, target->width, target->height, DESKTOP_BG);
    xenon_surface_str(target, 20, 20, "Xenon Desktop", TEXT_WHITE);
    xenon_surface_str(target, 20, 40, "Development build", TEXT_MUTED);

    if (target->height >= TASKBAR_H) {
        xenon_surface_rect(target, 0, target->height - TASKBAR_H, target->width, TASKBAR_H, DESKTOP_PANEL);
        xenon_surface_rect(target, 10, target->height - 31, 22, 22, ACCENT_BLUE);
        xenon_surface_str(target, 42, target->height - 27, "Xenon", TEXT_WHITE);
        xenon_surface_str(target, 115, target->height - 27, "xeterm", TEXT_MUTED);
    }
}

b8 compositor_init(xenon_surface_t* surface) {
    if (!surface) { return false; }

    target = surface;
    cursor_x = (i32)(surface->width / 2);
    cursor_y = (i32)(surface->height / 2);
    previous_buttons = 0;
    dragging_id = -1;

    window_server_set_invalidate_callback(compositor_invalidate);
    clamp_cursor();
    frame_invalidated = true;

    return true;
}

// IMPORTANT NOTE: called from window server
void compositor_invalidate() { frame_invalidated = true; }

void compositor_render() {
    if (!target || !frame_invalidated) { return; }

    // copy metadata under the window-server lock, the surface lifetime msut remain valid until this frame finishes, still a prototype
    snapshot_count = window_server_snapshot(snapshots, COMPOSITOR_MAX_WINDOWS);

    render_desktop_background();

    for (u32 i = 0; i < snapshot_count; ++i) {
        render_window(target, &snapshots[i]);
    }

    xenon_surface_image_data(target, (u32)cursor_x, (u32)cursor_y, mouse_cursor_data, CURSOR_W, CURSOR_H, true);

    xenon_surface_present(target);
    frame_invalidated = false;
}

static i32 hit_test(i32 x, i32 y) {
    // snapshots are back-to-front, so search in reverse
    for (i32 i = (i32)snapshot_count - 1; i >= 0; i--) {
        window_snapshot_t* w = &snapshots[i];

        // TODO:
        // if (w->state == WINDOW_STATE_HIDDEN || w->state == WINDOW_STATE_MINIMIZED) {
        //     continue;
        // }

        i32 wx = (i32)w->rect.x;
        i32 wy = (i32)w->rect.y;

        if (x >= wx && x < wx + (i32)w->rect.width && y >= wy && y < wy + (i32)w->rect.height) {
            return i;
        }
    }

    return -1;
}


static void clamp_window_position(i32* x, i32* y, i32 width, i32 height) {
    if (!target || !x || !y) {
        return;
    }

    i32 max_x = (i32)target->width - width;
    i32 max_y = (i32)target->height - height;

    // if a window is larger than the screen, pin it to the origin
    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;

    if (*x < 0) *x = 0;
    if (*y < 0) *y = 0;
    if (*x > max_x) *x = max_x;
    if (*y > max_y) *y = max_y;
}

static void process_mouse_event(const input_mouse_event_t* evt) {
    i32 old_x = cursor_x;
    i32 old_y = cursor_y;

    if (evt->flags & INPUT_MOUSE_FLAG_RELATIVE) {
        cursor_x += evt->x_value;
        cursor_y += evt->y_value;
    } else {
        cursor_x = evt->x_value;
        cursor_y = evt->y_value;
    }

    clamp_cursor();

    b8 left_down = (evt->buttons & MOUSE_BUTTON_LEFT) != 0;
    b8 left_was_down = (previous_buttons & MOUSE_BUTTON_LEFT) != 0;

    if (left_down && !left_was_down) {
        snapshot_count = window_server_snapshot(snapshots, COMPOSITOR_MAX_WINDOWS);

        i32 index = hit_test(cursor_x, cursor_y);

        if (index >= 0) {
            window_snapshot_t* hit = &snapshots[index];
            i32 wx = (i32)hit->rect.x;
            i32 wy = (i32)hit->rect.y;

            // close button hit test, queue CLOSE_REQUEST don't destroy the window here
            if (cursor_y >= wy + 9 && cursor_y < wy + 19 && cursor_x >= wx + (i32)hit->rect.width - 23 && cursor_x < wx + (i32)hit->rect.width - 13) {
                window_request_close(hit->handle);
            } else {
                window_raise(hit->handle);
                // only the title bar initiates dragging
                if (cursor_y >= wy &&
                    cursor_y < wy + TITLEBAR_H) {
                    dragging_id = (i32)hit->id;
                    drag_offset_x = cursor_x - wx;
                    drag_offset_y = cursor_y - wy;
                }
            }
        }
    }

    if (left_down && dragging_id >= 0) {
        snapshot_count = window_server_snapshot(snapshots, COMPOSITOR_MAX_WINDOWS);

        for (u32 i = 0; i < snapshot_count; i++) {
            if ((i32)snapshots[i].id != dragging_id) {
                continue;
            }

            i32 new_x = cursor_x - drag_offset_x;
            i32 new_y = cursor_y - drag_offset_y;

            clamp_window_position(&new_x, &new_y, (i32)snapshots[i].rect.width, (i32)snapshots[i].rect.height);
            window_set_position(snapshots[i].handle, new_x, new_y);
            break;
        }
    }

    if (!left_down) {
        dragging_id = -1;
    }

    if (cursor_x != old_x || cursor_y != old_y || evt->buttons != previous_buttons || evt->wheel != 0) {
        compositor_invalidate();
    }

    previous_buttons = evt->buttons;
}

void compositor_process_events(void) {
    input_mouse_event_t evt;

    while (input_pop_mouse_event(&evt)) { process_mouse_event(&evt); }
}
