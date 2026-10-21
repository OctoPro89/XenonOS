#include <windowing/compositor/compositor.h>
#include <windowing/compositor/compositor_image.h>
#include <windowing/window/window.h>
#include <windowing/window/internal.h>
#include <drivers/input/input.h>
#include <task.h>
#include <xlibc/string.h>
#include <memory/heap.h>
#include <graphics/graphics.h>

#include <arch/x86_64/sync/sync.h>
#include <kernel.h>

#include <de/mouse_cursor.h>

// TODO: partial redrawing
// TODO: use window damaging

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
#define TITLEBAR_H 30
#define WINDOW_RADIUS 10
#define MOUSE_BUTTON_LEFT (1u << 0)

#define TASKBAR_HEIGHT       52
#define TASKBAR_BOTTOM       12
#define TASKBAR_MAX_WIDTH    460
#define TASKBAR_MIN_WIDTH    180
#define TASKBAR_RADIUS       15
#define TASKBAR_BORDER       0xFF465164
#define TASKBAR_BACKGROUND   0xFF202938
#define TASKBAR_SEPARATOR    0xFF465164

#define POWER_BUTTON_SIZE       32
#define POWER_BUTTON_MARGIN     10
#define POWER_MENU_WIDTH        176
#define POWER_MENU_ROW_HEIGHT   34
#define POWER_MENU_PADDING      6
#define POWER_MENU_HEIGHT       (POWER_MENU_PADDING * 2 + POWER_MENU_ROW_HEIGHT * 2)

#define POWER_BUTTON_HOVER      0xFF35445C
#define POWER_MENU_BACKGROUND   0xFF202938
#define POWER_MENU_BORDER       0xFF465164
#define POWER_MENU_HOVER        0xFF35445C
#define POWER_MENU_DANGER       0xFFF87171
#define POWER_MENU_DANGER_BG    0xFF3B2932

#define COMPOSITOR_MAX_WINDOWS WINDOW_MAX_COUNT

static compositor_image_t desktop_wallpaper;

static xenon_surface_t* target;
static u32 frame_invalidated = 1;

static i32 cursor_x, cursor_y;
static u32 previous_buttons;
static i32 dragging_id = -1;
static i32 drag_offset_x, drag_offset_y;

// all current window surface
static u32 surface_invalid = WINDOW_SURFACE_INVALID_HANDLE;
static window_surface_t surfaces[COMPOSITOR_MAX_WINDOWS];
static u32 surfaces_count;
spinlock_t surfaces_lock;

// stable per-frame copies of window meta
static window_snapshot_t snapshots[COMPOSITOR_MAX_WINDOWS];
static u32 snapshot_count;

// TODO: structure this better and less of a hack-in
typedef enum {
    POWER_ACTION_NONE = 0,
    POWER_ACTION_SHUTDOWN,
    POWER_ACTION_REBOOT
} power_action_t;

static b8 power_menu_open;
static b8 power_button_hovered;
static power_action_t power_action_hovered;

static void compositor_mark_surface_invalid(window_surface_handle_t handle);

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

static b8 get_taskbar_rect(const xenon_surface_t* s, i32* out_x, i32* out_y, i32* out_width, i32* out_height) {
    if (!s || !out_x || !out_y || !out_width || !out_height || s->height < TASKBAR_HEIGHT + TASKBAR_BOTTOM) {
        return false;
    }

    i32 width = TASKBAR_MAX_WIDTH;
    i32 max_width = (i32)s->width - 24;

    if (width > max_width) {
        width = max_width;
    }

    if (width < TASKBAR_MIN_WIDTH) {
        width = max_width;
    }

    if (width <= 0) {
        return false;
    }

    *out_width = width;
    *out_height = TASKBAR_HEIGHT;
    *out_x = ((i32)s->width - width) / 2;
    *out_y = (i32)s->height - TASKBAR_HEIGHT - TASKBAR_BOTTOM;

    return true;
}

static void draw_power_icon(xenon_surface_t* s, i32 x, i32 y, i32 size, u32 color) {
    const i32 cx = x + size / 2;
    const i32 cy = y + size / 2;
    const i32 outer = 8;
    const i32 inner = 5;

    for (i32 py = -outer; py <= outer; ++py) {
        for (i32 px = -outer; px <= outer; ++px) {
            const i32 d2 = px * px + py * py;

            if (d2 > outer * outer || d2 < inner * inner) {
                continue;
            }

            // open the ring at the top
            if (py < 0 && px >= -2 && px <= 2) {
                continue;
            }

            const i32 sx = cx + px;
            const i32 sy = cy + py;

            if (sx >= 0 && sy >= 0 && sx < (i32)s->width && sy < (i32)s->height) {
                xenon_surface_rect(s, (u32)sx, (u32)sy, 1, 1, color);
            }
        }
    }

    round_rect(s, (u32)(cx - 1), (u32)(cy - 10), 3, 10, 2, color);
}

static b8 get_power_button_rect(i32* out_x, i32* out_y, i32* out_size) {
    i32 tx, ty, tw, th;

    if (!target || !get_taskbar_rect(target, &tx, &ty, &tw, &th)) {
        return false;
    }

    const i32 size = POWER_BUTTON_SIZE;

    *out_x = tx + tw - POWER_BUTTON_MARGIN - size;
    *out_y = ty + (th - size) / 2;
    *out_size = size;

    return true;
}

static void get_power_menu_rect(i32* out_x, i32* out_y) {
    i32 bx, by, bs;

    if (!get_power_button_rect(&bx, &by, &bs)) {
        *out_x = 0;
        *out_y = 0;
        return;
    }

    *out_x = bx + bs - POWER_MENU_WIDTH;
    *out_y = by - POWER_MENU_HEIGHT - 20;
}

static void render_power_button(xenon_surface_t* s) {
    i32 x, y, size;

    if (!s || !get_power_button_rect(&x, &y, &size)) {
        return;
    }

    const b8 active = power_button_hovered || power_menu_open;

    if (active) {
        // round_rect(s, x, y, size, size, 10, POWER_BUTTON_BORDER);

        round_rect(s, x + 1, y + 1, size - 2, size - 2, 9, POWER_BUTTON_HOVER);
    }

    const i32 icon_size = 20;
    const i32 icon_x = x + (size - icon_size) / 2;
    const i32 icon_y = y + (size - icon_size) / 2;

    draw_power_icon(s, icon_x, icon_y, icon_size, TEXT_WHITE);
}

static void render_power_menu(xenon_surface_t* s) {
    if (!power_menu_open || !s) {
        return;
    }

    i32 x, y;
    get_power_menu_rect(&x, &y);

    round_rect(s, x, y, POWER_MENU_WIDTH, POWER_MENU_HEIGHT, 12, POWER_MENU_BORDER);

    round_rect(s, x + 1, y + 1, POWER_MENU_WIDTH - 2, POWER_MENU_HEIGHT - 2, 11, POWER_MENU_BACKGROUND);

    const i32 row_x = x + POWER_MENU_PADDING;
    const i32 row_width = POWER_MENU_WIDTH - POWER_MENU_PADDING * 2;

    const i32 shutdown_y = y + POWER_MENU_PADDING;
    const i32 reboot_y = shutdown_y + POWER_MENU_ROW_HEIGHT;

    if (power_action_hovered == POWER_ACTION_REBOOT) {
        round_rect(s, row_x, reboot_y, row_width, POWER_MENU_ROW_HEIGHT, 7, POWER_MENU_HOVER);
    }

    if (power_action_hovered == POWER_ACTION_SHUTDOWN) {
        round_rect(s, row_x, shutdown_y, row_width, POWER_MENU_ROW_HEIGHT, 7, POWER_MENU_DANGER_BG);
    }

    const u32 shutdown_icon_color = power_action_hovered == POWER_ACTION_SHUTDOWN ? POWER_MENU_DANGER : TEXT_MUTED;

    const u32 reboot_icon_color = power_action_hovered == POWER_ACTION_REBOOT ? TEXT_WHITE : TEXT_MUTED;

    draw_power_icon(s, x + 15, shutdown_y + 7, 20, shutdown_icon_color);
    
    xenon_surface_str(s, x + 43, shutdown_y + 12, "Shut down", power_action_hovered == POWER_ACTION_SHUTDOWN ? TEXT_WHITE : TEXT_MUTED);

    const i32 rcx = x + 25;
    const i32 rcy = reboot_y + 17;

    for (i32 py = -6; py <= 6; ++py) {
        for (i32 px = -6; px <= 6; ++px) {
            const i32 d2 = px * px + py * py;

            if (d2 < 20 || d2 > 36) {
                continue;
            }

            if (px > 0 && py < 0) {
                continue;
            }

            xenon_surface_rect(s, (u32)(rcx + px), (u32)(rcy + py), 1, 1, power_action_hovered == POWER_ACTION_REBOOT ? TEXT_WHITE : TEXT_MUTED);
        }
    }

    xenon_surface_rect(s, (u32)(rcx + 3), (u32)(rcy - 8), 5, 2, TEXT_MUTED);
    xenon_surface_rect(s, (u32)(rcx + 6), (u32)(rcy - 8), 2, 5, TEXT_MUTED);
    xenon_surface_str(s, x + 43, reboot_y + 12, "Reboot", power_action_hovered == POWER_ACTION_REBOOT ? TEXT_WHITE : TEXT_MUTED);
}

static void render_taskbar(xenon_surface_t* s) {
    if (!s || s->width < 1 || s->height < TASKBAR_HEIGHT + TASKBAR_BOTTOM) {
        return;
    }

    i32 x, y, width, height;

    if (!get_taskbar_rect(s, &x, &y, &width, &height)) {
        return;
    }

    // outer border and dock body
    round_rect(s, x, y, width, height, TASKBAR_RADIUS, TASKBAR_BORDER);

    round_rect(s, x + 1, y + 1, width - 2, height - 2, TASKBAR_RADIUS - 1, TASKBAR_BACKGROUND);

    // temporary placeholder until app icons are implemented
    const i32 center_y = y + (height - 8) / 2;

    xenon_surface_str(s, x + 18, center_y, "Xenon", TEXT_WHITE);

    // subtle divider between the desktop identity and future dock items
    const i32 separator_x = x + 100;
    const i32 separator_x2 = x + width - 50;

    xenon_surface_rect(s, (u32)separator_x, (u32)(y + 12), 1, (u32)(height - 24), TASKBAR_SEPARATOR);

    xenon_surface_str(s, separator_x + 14, center_y, "Desktop", TEXT_MUTED);

    xenon_surface_rect(s, (u32)separator_x2, (u32)(y + 12), 1, (u32)(height - 24), TASKBAR_SEPARATOR);

    render_power_button(s);
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
    const window_surface_t* src = win->surface;
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

        // TODO: fix
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
    if (desktop_wallpaper.pixels) {
        compositor_image_draw(target, &desktop_wallpaper, 0, 0, 0);
    }
    else {
        xenon_surface_rect(target, 0, 0, target->width, target->height, DESKTOP_BG);
        xenon_surface_str(target, 20, 20, "Xenon Desktop", TEXT_WHITE);
        xenon_surface_str(target, 20, 40, "Development build", TEXT_MUTED);
    }

    render_taskbar(target);
}

b8 compositor_init(xenon_surface_t* surface) {
    if (!surface) { return false; }

    target = surface;
    cursor_x = (i32)(surface->width / 2);
    cursor_y = (i32)(surface->height / 2);
    previous_buttons = 0;
    dragging_id = -1;

    spin_lock_init(&surfaces_lock);

    window_server_set_invalidate_callback(compositor_invalidate);
    clamp_cursor();
    __atomic_store_n(&frame_invalidated, 1, __ATOMIC_RELEASE);

    return true;
}

// IMPORTANT NOTE: called from window server, must use atomics
void compositor_invalidate() { __atomic_store_n(&frame_invalidated, 1, __ATOMIC_RELEASE); }

void compositor_render() {
    // also sets frame_invalidated to false
    if (!target || !__atomic_exchange_n(&frame_invalidated, 0, __ATOMIC_ACQUIRE)) { return; }

    // TODO: remove this sucks
    window_surface_handle_t invalid_surface = WINDOW_SURFACE_INVALID_HANDLE;

    // copy metadata under the window-server lock, the surface lifetime msut remain valid until this frame finishes, still a prototype
    snapshot_count = window_server_snapshot(snapshots, COMPOSITOR_MAX_WINDOWS);

    render_desktop_background();

    u64 flags = 0;
    for (u32 i = 0; i < snapshot_count; ++i) {
        // TODO: maybe better solution idk
        spin_lock_irqsave(&surfaces_lock, &flags);
        invalid_surface = __atomic_load_n(&surface_invalid, __ATOMIC_SEQ_CST); // TODO: test this behavior
        if (invalid_surface == snapshots[i].surface->handle) {
            spin_unlock_irqrestore(&surfaces_lock, flags);
            continue; // this surface has been marked invalid so skip it
        }
        spin_unlock_irqrestore(&surfaces_lock, flags);

        render_window(target, &snapshots[i]);
        window_clear_damage(snapshots[i].handle);
    }

    render_power_menu(target);

    xenon_surface_image_data(target, (u32)cursor_x, (u32)cursor_y, mouse_cursor_data, CURSOR_W, CURSOR_H, true);

    xenon_surface_present(target);

    if (invalid_surface != WINDOW_SURFACE_INVALID_HANDLE) {
        void* surface_pixels_ptr = NULL;
        spin_lock_irqsave(&surfaces_lock, &flags);
        surface_pixels_ptr = surfaces[invalid_surface].pixels;
        // mark invalid without freeing in lock (expensive)
        surfaces[invalid_surface].pixels = NULL;
        invalid_surface = WINDOW_SURFACE_INVALID_HANDLE;
        spin_unlock_irqrestore(&surfaces_lock, flags);
        if (surface_pixels_ptr) {
            kfree(surface_pixels_ptr); // IMPORTANT NOTE: DO NOT CALL IN LOCK, not cheap call
        }
    }
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

static __hint_inline__ b8 point_in_rect(i32 px, i32 py, i32 x, i32 y, i32 width, i32 height) {
    return px >= x && py >= y && px < x + width && py < y + height;
}

static power_action_t power_menu_hit_test(i32 px, i32 py) {
    i32 x, y;
    get_power_menu_rect(&x, &y);

    if (!point_in_rect(px, py, x, y, POWER_MENU_WIDTH, POWER_MENU_HEIGHT)) {
        return POWER_ACTION_NONE;
    }

    const i32 row_y = y + POWER_MENU_PADDING;

    if (point_in_rect(px, py + 0, x + POWER_MENU_PADDING, row_y, POWER_MENU_WIDTH - POWER_MENU_PADDING * 2, POWER_MENU_ROW_HEIGHT)) {
        return POWER_ACTION_SHUTDOWN;
    }

    if (point_in_rect(px, py, x + POWER_MENU_PADDING, row_y + POWER_MENU_ROW_HEIGHT, POWER_MENU_WIDTH - POWER_MENU_PADDING * 2, POWER_MENU_ROW_HEIGHT)) {
        return POWER_ACTION_REBOOT;
    }

    return POWER_ACTION_NONE;
}

static b8 power_button_hit_test(i32 px, i32 py) {
    i32 x, y, size;

    if (!get_power_button_rect(&x, &y, &size)) {
        return false;
    }

    return point_in_rect(px, py, x, y, size, size);
}

#include <power/power.h>

static b8 handle_power_click(i32 px, i32 py) {
    if (power_menu_open) {
        power_action_t action = power_menu_hit_test(px, py);

        if (action != POWER_ACTION_NONE) {
            power_menu_open = false;
            power_action_hovered = POWER_ACTION_NONE;
            compositor_invalidate();

            if (action == POWER_ACTION_SHUTDOWN) {
                shutdown();
            } else if (action == POWER_ACTION_REBOOT) {
                reboot();
            }

            return true;
        }

        power_menu_open = false;
        power_action_hovered = POWER_ACTION_NONE;

        if (power_button_hit_test(px, py)) {
            compositor_invalidate();
            return true;
        }
    }

    if (power_button_hit_test(px, py)) {
        power_menu_open = true;
        power_action_hovered = POWER_ACTION_NONE;
        compositor_invalidate();
        return true;
    }

    return false;
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

    b8 old_button_hovered = power_button_hovered;
    power_button_hovered = power_button_hit_test(cursor_x, cursor_y);

    power_action_t old_action_hovered = power_action_hovered;

    if (power_menu_open) {
        power_action_hovered =
            power_menu_hit_test(cursor_x, cursor_y);
    } else {
        power_action_hovered = POWER_ACTION_NONE;
    }

    if (old_button_hovered != power_button_hovered ||
        old_action_hovered != power_action_hovered) {
        compositor_invalidate();
    }

    if (left_down && !left_was_down) {
        if (!handle_power_click(cursor_x, cursor_y)) {
            snapshot_count =
                window_server_snapshot(snapshots, COMPOSITOR_MAX_WINDOWS);

            i32 index = hit_test(cursor_x, cursor_y);

            if (index >= 0) {
                window_snapshot_t* hit = &snapshots[index];
                i32 wx = (i32)hit->rect.x;
                i32 wy = (i32)hit->rect.y;

                if (cursor_y >= wy + 9 &&
                    cursor_y < wy + 19 &&
                    cursor_x >= wx + (i32)hit->rect.width - 23 &&
                    cursor_x < wx + (i32)hit->rect.width - 13) {
                    window_server_request_close(hit->handle);
                } else {
                    window_server_raise(hit->handle);

                    if (cursor_y >= wy &&
                        cursor_y < wy + TITLEBAR_H) {
                        dragging_id = (i32)hit->id;
                        drag_offset_x = cursor_x - wx;
                        drag_offset_y = cursor_y - wy;
                    }
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
            window_server_set_position(snapshots[i].handle, new_x, new_y);
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

void compositor_process_events() {
    input_mouse_event_t evt;

    while (input_pop_mouse_event(&evt)) { process_mouse_event(&evt); }
}

const window_surface_t* compositor_create_surface_for_window(window_t* window) {
    // NOTE: window_server's lock is held here
    if (!window) { return NULL; }
    
    window_rect_t rect = window->rect; // DON'T DOUBLE LOCK | window_server_get_rect(window);

    if (rect.width <= 2 || rect.height <= 31) {
        return NULL;
    }

    // TODO: change if needed
    u32 width = (u32)rect.width - 2;
    u32 height = (u32)rect.height - 31;

    size_t stride = (size_t)width * sizeof(u32);
    size_t bytes = stride * height;

    if (!width || !height || stride < width * sizeof(u32)) {
        return NULL;
    }

    u32* pixels = kmalloc(bytes);
    if (!pixels) {
        return NULL;
    }

    u64 flags = 0;
    spin_lock_irqsave(&surfaces_lock, &flags);

    __atomic_add_fetch(&surfaces[surfaces_count].refcount, 1, __ATOMIC_RELAXED);

    surfaces[surfaces_count] = (window_surface_t) {
        .width = width,
        .height = height,
        .stride = (u32)stride,
        .format = WINDOW_PIXEL_FORMAT_RGBX8888,
        .handle = (window_surface_handle_t)surfaces_count, // TODO: better
        .pixels = pixels
    };
    
    if (!window_server_set_surface(window, &surfaces[surfaces_count])) {
        surfaces[surfaces_count].pixels = NULL;
        kfree(pixels);
        return NULL;
    }

    const window_surface_t* retval = &surfaces[surfaces_count];

    ++surfaces_count;

    spin_unlock_irqrestore(&surfaces_lock, flags);

    return (const window_surface_t*)retval;
}

void compositor_release_surface(window_surface_handle_t surface_handle) {
    if (surface_handle == WINDOW_SURFACE_INVALID_HANDLE) { return; }
    if (__atomic_sub_fetch(&surfaces[surface_handle].refcount, 1, __ATOMIC_ACQ_REL) == 0) {
        compositor_mark_surface_invalid(surface_handle);
    }
}

static void compositor_mark_surface_invalid(window_surface_handle_t handle) {
    // TODO: probably handle shouldn't be an index into live shit
    // TODO: terrible assumption but don't assume that two windows don't close on the same frame
    __atomic_store_n(&surface_invalid, (u32)handle, __ATOMIC_SEQ_CST);
}

b8 compositor_set_wallpaper(const char* path) {
    if (!target || !path) {
        return false;
    }

    compositor_image_t source = { 0 };
    compositor_image_t scaled = { 0 };

    if (!compositor_image_load_file(path, &source)) {
        return false;
    }

    if (!compositor_image_scale_nearest(&source, target->width, target->height, &scaled)) {
        compositor_image_destroy(&source);
        return false;
    }

    compositor_image_destroy(&source);

    // wallpapers are opaque, avoid per-pixel alpha blending
    size_t count = (size_t)scaled.width * scaled.height;

    for (size_t i = 0; i < count; ++i) {
        scaled.pixels[i] |= 0xFF000000;
    }

    // replace the previous wallpaper only after successful loading
    compositor_image_destroy(&desktop_wallpaper);
    desktop_wallpaper = scaled;

    compositor_invalidate();
    return true;
}