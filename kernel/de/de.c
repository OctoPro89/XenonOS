#include <de/de.h>
#include <de/surface.h>
#include <de/mouse_cursor.h>
#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <drivers/input/input.h>
#include <task.h>
#include <arch/x86_64/io.h>

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

typedef struct {
    i32 x, y, w, h;
    const char* title;
    b8 open;
} desktop_window_t;

static desktop_window_t windows[] = {
    { 40, 40, 420, 280, "xeterm", true },
    { 110, 100, 300, 180, "System Info", true }
};

#define WINDOW_COUNT (sizeof(windows) / sizeof(windows[0]))

static i32 dragging_window = -1;
static i32 drag_offset_x, drag_offset_y;

static void clamp_cursor(const xenon_surface_t* s, i32* x, i32* y) {
    i32 max_x = s->width > CURSOR_W ? (i32)s->width - CURSOR_W : 0;
    i32 max_y = s->height > CURSOR_H ? (i32)s->height - CURSOR_H : 0;

    if (*x < 0) *x = 0;
    if (*y < 0) *y = 0;
    if (*x > max_x) *x = max_x;
    if (*y > max_y) *y = max_y;
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

static void render_window(xenon_surface_t* s, const desktop_window_t* win) {
    if (!win->open || win->w < 4 || win->h < 4) return;

    i32 x = win->x, y = win->y, w = win->w, h = win->h;
    i32 border = 1;

    round_rect(s, x, y, w, h, WINDOW_RADIUS, WINDOW_BORDER);
    round_rect(s, x + border, y + border, w - 2, h - 2, WINDOW_RADIUS - 1, WINDOW_BODY);
    round_rect(s, x + border, y + border, w - 2, TITLEBAR_H, WINDOW_RADIUS - 1, WINDOW_TITLE);

    if (TITLEBAR_H > WINDOW_RADIUS)
        xenon_surface_rect(s, x + border, y + WINDOW_RADIUS, w - 2, TITLEBAR_H - WINDOW_RADIUS + 1, WINDOW_TITLE);

    xenon_surface_str(s, x + 10, y + 9, win->title, TEXT_WHITE);
    xenon_surface_rect(s, x + w - 23, y + 9, 10, 10, ACCENT_RED);
    xenon_surface_rect(s, x + w - 41, y + 9, 10, 10, ACCENT_BLUE);
}

static void render_desktop(xenon_surface_t* s) {
    u32 width = s->width, height = s->height;

    xenon_surface_rect(s, 0, 0, width, height, DESKTOP_BG);
    xenon_surface_str(s, 20, 20, "Xenon Desktop", TEXT_WHITE);
    xenon_surface_str(s, 20, 40, "Development build", TEXT_MUTED);

    for (u32 i = 0; i < WINDOW_COUNT; i++)
        render_window(s, &windows[i]);

    if (height >= TASKBAR_H) {
        xenon_surface_rect(s, 0, height - TASKBAR_H, width, TASKBAR_H, DESKTOP_PANEL);
        xenon_surface_rect(s, 10, height - 31, 22, 22, ACCENT_BLUE);
        xenon_surface_str(s, 42, height - 27, "Xenon", TEXT_WHITE);
        xenon_surface_str(s, 115, height - 27, "xeterm", TEXT_MUTED);
    }
}

static void bring_to_front(u32 index) {
    desktop_window_t win = windows[index];

    for (u32 i = index; i + 1 < WINDOW_COUNT; i++)
        windows[i] = windows[i + 1];

    windows[WINDOW_COUNT - 1] = win;
}

static i32 window_at(i32 x, i32 y) {
    for (i32 i = (i32)WINDOW_COUNT - 1; i >= 0; i--) {
        desktop_window_t* w = &windows[i];

        if (w->open && x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h)
            return i;
    }

    return -1;
}

static void handle_mouse_press(i32 x, i32 y) {
    dragging_window = -1;

    i32 index = window_at(x, y);
    if (index < 0) return;

    desktop_window_t* w = &windows[index];

    if (y >= w->y + 9 && y < w->y + 19 && x >= w->x + w->w - 23 && x < w->x + w->w - 13) {
        w->open = false;
        return;
    }

    bring_to_front((u32)index);
    w = &windows[WINDOW_COUNT - 1];

    if (y >= w->y && y < w->y + TITLEBAR_H) {
        dragging_window = (i32)WINDOW_COUNT - 1;
        drag_offset_x = x - w->x;
        drag_offset_y = y - w->y;
    }
}

static void move_dragged_window(i32 x, i32 y, const xenon_surface_t* s) {
    if (dragging_window < 0) return;

    desktop_window_t* w = &windows[dragging_window];

    w->x = x - drag_offset_x;
    w->y = y - drag_offset_y;

    if (w->x + w->w > (i32)s->width) w->x = (i32)s->width - w->w;
    if (w->y + TITLEBAR_H > (i32)s->height)
        w->y = (i32)s->height - TITLEBAR_H;

    if (w->x < 0) w->x = 0;
    if (w->y < 0) w->y = 0;
}

static void present_desktop(xenon_surface_t* s, i32 x, i32 y) {
    render_desktop(s);
    xenon_surface_image_data(s, (u32)x, (u32)y, mouse_cursor_data, CURSOR_W, CURSOR_H, true);
    xenon_surface_present(s);
}

void desktop_env_task_entry(void* arg) {
    (void)arg;

    xenon_surface_t* s = NULL;
    xenon_surface_get(&s);
    if (!s) return;

    i32 cursor_x = (i32)(s->width / 2);
    i32 cursor_y = (i32)(s->height / 2);
    u16 previous_buttons = 0;

    clamp_cursor(s, &cursor_x, &cursor_y);
    present_desktop(s, cursor_x, cursor_y);

    input_mouse_event_t evt;

    for (;;) {
        b8 changed = false;

        while (input_pop_mouse_event(&evt)) {
            i32 old_x = cursor_x;
            i32 old_y = cursor_y;

            if (evt.flags & INPUT_MOUSE_FLAG_RELATIVE) {
                cursor_x += evt.x_value;
                cursor_y += evt.y_value;
            } else {
                cursor_x = evt.x_value;
                cursor_y = evt.y_value;
            }

            clamp_cursor(s, &cursor_x, &cursor_y);

            b8 left_down = (evt.buttons & MOUSE_BUTTON_LEFT) != 0;
            b8 left_was_down = (previous_buttons & MOUSE_BUTTON_LEFT) != 0;

            if (left_down && !left_was_down)
                handle_mouse_press(cursor_x, cursor_y);

            if (left_down && dragging_window >= 0)
                move_dragged_window(cursor_x, cursor_y, s);

            if (!left_down)
                dragging_window = -1;

            if (cursor_x != old_x || cursor_y != old_y ||
                evt.buttons != previous_buttons || evt.wheel != 0)
                changed = true;

            previous_buttons = evt.buttons;
        }

        if (changed)
            present_desktop(s, cursor_x, cursor_y);

        task_yield();
    }
}