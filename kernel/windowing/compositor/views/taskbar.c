#include <windowing/compositor/views/taskbar.h>
#include <windowing/compositor/compositor_image.h>
#include <windowing/compositor/compositor.h>
#include <windowing/compositor/helpers.h>
#include <windowing/compositor/colors.h>
#include <windowing/window/window.h>
#include <windowing/window/internal.h>
#include <drivers/input/input.h>
#include <xlibc/string.h>
#include <xlibc/ctype.h>
#include <memory/heap.h>
#include <graphics/graphics.h>
#include <filesystem/vfs/vfs.h>
#include <arch/x86_64/sync/sync.h>
#include <kernel.h>
#include <de/mouse_cursor.h>
#include <power/power.h>

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

typedef enum {
    POWER_ACTION_NONE = 0,
    POWER_ACTION_SHUTDOWN,
    POWER_ACTION_REBOOT
} power_action_t;

static b8 power_menu_open;
static b8 power_button_hovered;
static power_action_t power_action_hovered;

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

static b8 get_power_button_rect(xenon_surface_t* s, i32* out_x, i32* out_y, i32* out_size) {
    i32 tx, ty, tw, th;

    if (!s || !get_taskbar_rect(s, &tx, &ty, &tw, &th)) {
        return false;
    }

    const i32 size = POWER_BUTTON_SIZE;

    *out_x = tx + tw - POWER_BUTTON_MARGIN - size;
    *out_y = ty + (th - size) / 2;
    *out_size = size;

    return true;
}

static void get_power_menu_rect(xenon_surface_t* s, i32* out_x, i32* out_y) {
    i32 bx, by, bs;

    if (!get_power_button_rect(s, &bx, &by, &bs)) {
        *out_x = 0;
        *out_y = 0;
        return;
    }

    *out_x = bx + bs - POWER_MENU_WIDTH;
    *out_y = by - POWER_MENU_HEIGHT - 20;
}

static void render_power_button(xenon_surface_t* s) {
    i32 x, y, size;

    if (!s || !get_power_button_rect(s, &x, &y, &size)) {
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
    get_power_menu_rect(s, &x, &y);

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

static void render_taskbar_body(xenon_surface_t* s) {
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

static power_action_t power_menu_hit_test(xenon_surface_t* s, i32 px, i32 py) {
    i32 x, y;
    get_power_menu_rect(s, &x, &y);

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

static b8 power_button_hit_test(xenon_surface_t* s, i32 px, i32 py) {
    i32 x, y, size;

    if (!get_power_button_rect(s, &x, &y, &size)) {
        return false;
    }

    return point_in_rect(px, py, x, y, size, size);
}

static b8 handle_power_click(xenon_surface_t* s, i32 px, i32 py) {
    if (power_menu_open) {
        power_action_t action = power_menu_hit_test(s, px, py);

        if (action != POWER_ACTION_NONE) {
            power_menu_open = false;
            power_action_hovered = POWER_ACTION_NONE;
            compositor_invalidate();

            // TODO: Request poweroff
            if (action == POWER_ACTION_SHUTDOWN) {
                shutdown();
            } else if (action == POWER_ACTION_REBOOT) {
                reboot();
            }

            return true;
        }

        power_menu_open = false;
        power_action_hovered = POWER_ACTION_NONE;

        if (power_button_hit_test(s, px, py)) {
            compositor_invalidate();
            return true;
        }
    }

    if (power_button_hit_test(s, px, py)) {
        power_menu_open = true;
        power_action_hovered = POWER_ACTION_NONE;
        compositor_invalidate();
        return true;
    }

    return false;
}

void taskbar_render(xenon_surface_t* s) {
    render_taskbar_body(s);
}

b8 taskbar_handle_click(xenon_surface_t* s, i32 x, i32 y) {
    return handle_power_click(s, x, y);
}

void taskbar_process_mouse_events(xenon_surface_t* s, i32 x, i32 y) {
    b8 old_button_hovered = power_button_hovered;
    power_button_hovered = power_button_hit_test(s, x, y);

    power_action_t old_action_hovered = power_action_hovered;

    if (power_menu_open) {
        power_action_hovered = power_menu_hit_test(s, x, y);
    } else {
        power_action_hovered = POWER_ACTION_NONE;
    }

    if (old_button_hovered != power_button_hovered ||
        old_action_hovered != power_action_hovered) {
        compositor_invalidate();
    }
}

void taskbar_render_top_level(xenon_surface_t* s) {
    render_power_menu(s);
}