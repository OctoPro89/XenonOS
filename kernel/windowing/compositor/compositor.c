#include <windowing/compositor/compositor.h>
#include <windowing/compositor/compositor_image.h>
#include <windowing/compositor/views/taskbar.h>
#include <windowing/compositor/helpers.h>
#include <windowing/compositor/colors.h>
#include <windowing/window/window.h>
#include <windowing/window/internal.h>
#include <drivers/input/input.h>
#include <task.h>
#include <xlibc/string.h>
#include <xlibc/ctype.h>
#include <memory/heap.h>
#include <graphics/graphics.h>
#include <filesystem/vfs/vfs.h>
#include <arch/x86_64/sync/sync.h>
#include <kernel.h>
#include <de/mouse_cursor.h>

// TODO: partial redrawing
// TODO: use window damaging
// TODO: split some of these into separate files

#define CURSOR_W MOUSE_CURSOR_FRAME_WIDTH
#define CURSOR_H MOUSE_CURSOR_FRAME_HEIGHT
#define TITLEBAR_H 30
#define WINDOW_RADIUS 10
#define MOUSE_BUTTON_LEFT (1u << 0)

#define COMPOSITOR_MAX_WINDOWS WINDOW_MAX_COUNT

#define LAUNCHER_WIDTH          560
#define LAUNCHER_HEIGHT         360
#define LAUNCHER_VISIBLE_MAX    7
#define LAUNCHER_ROW_HEIGHT     27
#define LAUNCHER_QUERY_MAX      56
#define LAUNCHER_PATH_MAX       255

#define LAUNCHER_BORDER         0xFF56657A
#define LAUNCHER_BACKGROUND     0xFF1C2636
#define LAUNCHER_FIELD_BG       0xFF111827
#define LAUNCHER_ROW_SELECTED   0xFF315A84

#define HID_KEY_ENTER        0x28
#define HID_KEY_ESCAPE       0x29
#define HID_KEY_BACKSPACE    0x2A
#define HID_KEY_LEFT_SHIFT   0xE1
#define HID_KEY_RIGHT_SHIFT  0xE5
#define HID_KEY_LEFT_GUI     0xE3
#define HID_KEY_RIGHT_GUI    0xE7
#define HID_KEY_UP_ARROW     0x52
#define HID_KEY_DOWN_ARROW   0x51
#define HID_KEY_S            0x16

typedef struct launcher_program {
    struct launcher_program* next;
    char name[VFS_NAME_MAX + 1];
} launcher_program_t;

static launcher_program_t* launcher_programs;
static launcher_program_t* launcher_programs_tail;

static b8 launcher_scanned;
static b8 launcher_scan_error;
static b8 launcher_open;

static char launcher_query[LAUNCHER_QUERY_MAX + 1];
static u32 launcher_query_len;
static u32 launcher_selected;

static const char* launcher_status;

static b8 launcher_left_super_down;
static b8 launcher_right_super_down;
static b8 launcher_shift_down;

static b8 launcher_eat_keyup[256];

typedef b8 (*compositor_program_spawn_fn)(const char* absolute_path);

static compositor_program_spawn_fn launcher_spawn_callback;

static compositor_image_t desktop_wallpaper;
static u32 wallpaper_queued = 0;
char g_new_wallpaper_path[256];

static mutex_t wallpaper_mutex;

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

/*
 * User-process crash reports are copied into a small bounded queue. The
 * queue is protected by a tiny atomic lock because reporting can originate
 * outside the compositor's event/render path.
 */
#define CRASH_QUEUE_CAPACITY 8
#define CRASH_DIALOG_WIDTH   560
#define CRASH_DIALOG_HEIGHT  350
#define CRASH_DIALOG_BG      0xFF1C2636
#define CRASH_DIALOG_BORDER  0xFF718096
#define CRASH_DIALOG_PANEL   0xFF263449
#define CRASH_DIALOG_BUTTON  0xFF3B82F6
#define CRASH_DIALOG_BUTTON_HOVER 0xFF60A5FA
#define CRASH_DIALOG_TEXT    0xFFF8FAFC
#define CRASH_DIALOG_MUTED   0xFFB8C5D6

static spinlock_t crash_queue_lock;
static compositor_crash_report_t crash_queue[CRASH_QUEUE_CAPACITY];
static u32 crash_queue_head;
static u32 crash_queue_count;
static compositor_crash_report_t active_crash;
static b8 crash_dialog_visible;
static b8 crash_button_hovered;

static void compositor_mark_surface_invalid(window_surface_handle_t handle);

static void clamp_cursor() {
    i32 max_x = target->width > CURSOR_W ? (i32)target->width - CURSOR_W : 0;
    i32 max_y = target->height > CURSOR_H ? (i32)target->height - CURSOR_H : 0;

    if (cursor_x < 0) { cursor_x = 0; }
    if (cursor_y < 0) { cursor_y = 0; }
    if (cursor_x > max_x) { cursor_x = max_x; }
    if (cursor_y > max_y) { cursor_y = max_y; }
}

static b8 launcher_name_matches(const char* name) {
    if (!name) return false;

    if (launcher_query_len == 0) {
        return true;
    }

    for (u32 i = 0; name[i]; ++i) {
        u32 j = 0;

        while (launcher_query[j] && name[i + j] && tolower(name[i + j]) == tolower(launcher_query[j])) {
            ++j;
        }

        if (j == launcher_query_len) {
            return true;
        }
    }

    return false;
}

static b8 launcher_scan_bin() {
    VFS_DIR* dir = vfs_opendir("/bin");

    if (!dir) {
        return false;
    }

    launcher_scanned = true;

    VFS_DIRENT ent;
    int rc;

    while ((rc = vfs_readdir(dir, &ent)) > 0) {
        ent.name[VFS_NAME_MAX] = '\0';

        if (ent.type != VFS_TYPE_FILE) {
            continue;
        }

        if (!ent.name[0] || (ent.name[0] == '.' && ent.name[1] == '\0') || (ent.name[0] == '.' && ent.name[1] == '.' && ent.name[2] == '\0')) {
            continue;
        }

        u32 len = strlen(ent.name);

        // "/bin/" is 5 characters; VFS paths are limited to 255
        if (len + 5 > LAUNCHER_PATH_MAX) {
            launcher_scan_error = true;
            continue;
        }

        b8 invalid_name = false;

        for (u32 i = 0; i < len; ++i) {
            if (ent.name[i] == '/') {
                invalid_name = true;
                break;
            }
        }

        if (invalid_name) {
            continue;
        }

        launcher_program_t* item = kmalloc(sizeof(*item));

        if (!item) {
            launcher_scan_error = true;
            break;
        }

        for (u32 i = 0; i <= len; ++i) {
            item->name[i] = ent.name[i];
        }

        item->next = NULL;

        if (launcher_programs_tail)
            launcher_programs_tail->next = item;
        else
            launcher_programs = item;

        launcher_programs_tail = item;
    }

    if (rc < 0) {
        launcher_scan_error = true;
    }

    vfs_closedir(dir);
    return true;
}

static u32 launcher_match_count() {
    u32 count = 0;

    for (launcher_program_t* p = launcher_programs; p; p = p->next) {
        if (launcher_name_matches(p->name)) {
            ++count;
        }
    }

    return count;
}

// rank is the zero-based position in the filtered results
static launcher_program_t* launcher_match_at(u32 rank) {
    for (launcher_program_t* p = launcher_programs; p; p = p->next) {
        if (!launcher_name_matches(p->name)) {
            continue;
        }

        if (rank == 0)
            return p;

        --rank;
    }

    return NULL;
}

static void launcher_open_search() {
    launcher_open = true;
    launcher_query_len = 0;
    launcher_query[0] = '\0';
    launcher_selected = 0;
    launcher_status = NULL;

    if (!launcher_scanned) {
        if (false) {
            launcher_status = "Cannot open /bin";
        } else if (launcher_scan_error) {
            launcher_status = "Warning: program list incomplete";
        }
    }

    compositor_invalidate();
}

static void render_launcher(xenon_surface_t* s) {
    if (!s || !launcher_open) {
        return;
    }

    i32 w = LAUNCHER_WIDTH;
    i32 h = LAUNCHER_HEIGHT;

    if (w > (i32)s->width - 16) {
        w = (i32)s->width - 16;
    }

    if (h > (i32)s->height - 16) {
        h = (i32)s->height - 16;
    }

    if (w < 300 || h < 220) {
        return;
    }

    i32 x = ((i32)s->width - w) / 2;
    i32 y = ((i32)s->height - h) / 2;

    // dim desktop, TODO:
    // xenon_surface_rect(s, 0, 0, s->width, s->height, 0x99000000);
    round_rect(s, x, y, w, h, 16, LAUNCHER_BORDER);
    round_rect(s, x + 1, y + 1, w - 2, h - 2, 15, LAUNCHER_BACKGROUND);
    xenon_surface_str(s, x + 20, y + 17, "Search Programs", TEXT_WHITE);
    xenon_surface_str(s, x + 20, y + 33, "Installed applications in /bin", TEXT_MUTED);
    round_rect(s, x + 14, y + 48, w - 28, 34, 7, LAUNCHER_FIELD_BG);

    const char* search_text = launcher_query_len ? launcher_query : "Type a program name...";

    xenon_surface_str(s, x + 25, y + 61, search_text, launcher_query_len ? TEXT_WHITE : TEXT_MUTED);

    u32 total = launcher_match_count();

    u32 available_rows = (u32)(h - 132) / LAUNCHER_ROW_HEIGHT;

    if (available_rows > LAUNCHER_VISIBLE_MAX) {
        available_rows = LAUNCHER_VISIBLE_MAX;
    }

    if (available_rows == 0) {
        available_rows = 1;
    }

    u32 first = 0;

    if (launcher_selected >= available_rows) {
        first = launcher_selected - available_rows + 1;
    }

    u32 rows = total > first ? total - first : 0;

    if (rows > available_rows) {
        rows = available_rows;
    }

    for (u32 row = 0; row < rows; ++row) {
        u32 rank = first + row;
        launcher_program_t* item = launcher_match_at(rank);

        if (!item) {
            break;
        }

        i32 row_y = y + 94 + (i32)row * LAUNCHER_ROW_HEIGHT;

        if (rank == launcher_selected) {
            round_rect(s, x + 12, row_y, w - 24, 25, 6, LAUNCHER_ROW_SELECTED);
        }

        // clip names
        char display_name[57];
        u32 i = 0;

        while (item->name[i] && i < 53) {
            display_name[i] = item->name[i];
            ++i;
        }

        if (item->name[i]) {
            display_name[i++] = '.';
            display_name[i++] = '.';
            display_name[i++] = '.';
        }

        display_name[i] = '\0';

        xenon_surface_str(s, x + 24, row_y + 8, display_name, rank == launcher_selected ? TEXT_WHITE : TEXT_MUTED);
    }

    if (total == 0) {
        xenon_surface_str(s, x + 24, y + 108, "No matching programs", TEXT_MUTED);
    }

    const char* footer = launcher_status ? launcher_status : "Enter: launch   Up/Down: select   Esc: close";

    xenon_surface_str(s, x + 18, y + h - 23, footer, launcher_status ? 0xFFFCA5A5 : TEXT_MUTED);
}

static const char* crash_exception_name(u64 vector) {
    static const char* const names[32] = {
        "Divide-by-zero Error", "Debug", "Non-maskable Interrupt", "Breakpoint",
        "Overflow", "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
        "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
        "Stack-Segment Fault", "General Protection Fault", "Page Fault", "Reserved",
        "x87 Floating-Point Exception", "Alignment Check", "Machine Check",
        "SIMD Floating-Point Exception", "Virtualization Exception",
        "Control Protection Exception", "Reserved", "Reserved", "Reserved",
        "Reserved", "Reserved", "Reserved", "Hypervisor Injection Exception",
        "VMM Communication Exception", "Security Exception", "Reserved"
    };
    return vector < 32 ? names[vector] : "Unknown CPU Exception";
}

static void crash_draw_hex(xenon_surface_t* s, i32 x, i32 y, const char* label, u64 value) {
    static const char digits[] = "0123456789ABCDEF";
    char text[40];
    u32 n = 0;
    while (label[n] && n < 8) {
        text[n] = label[n];
        ++n;
    }
    text[n++] = ' ';
    text[n++] = '0';
    text[n++] = 'x';
    for (i32 i = 15; i >= 0; --i) {
        text[n++] = digits[(value >> (i * 4)) & 0xF];
    }
    text[n] = '\0';
    xenon_surface_str(s, x, y, text, CRASH_DIALOG_MUTED);
}

static void get_crash_dialog_rect(i32* x, i32* y, i32* w, i32* h) {
    *w = target && target->width < CRASH_DIALOG_WIDTH ? (i32)target->width - 16 : CRASH_DIALOG_WIDTH;
    *h = target && target->height < CRASH_DIALOG_HEIGHT ? (i32)target->height - 16 : CRASH_DIALOG_HEIGHT;
    if (*w < 220) *w = target ? (i32)target->width : 220;
    if (*h < 180) *h = target ? (i32)target->height : 180;
    *x = target ? ((i32)target->width - *w) / 2 : 0;
    *y = target ? ((i32)target->height - *h) / 2 : 0;
}

static b8 crash_ok_hit_test(i32 px, i32 py) {
    i32 x, y, w, h;
    if (!crash_dialog_visible || !target) return false;
    get_crash_dialog_rect(&x, &y, &w, &h);
    return point_in_rect(px, py, x + w - 112, y + h - 42, 88, 28);
}

static void crash_promote_next_locked() {
    if (crash_queue_count == 0) {
        crash_dialog_visible = false;
        crash_button_hovered = false;
        return;
    }
    active_crash = crash_queue[crash_queue_head];
    crash_queue_head = (crash_queue_head + 1) % CRASH_QUEUE_CAPACITY;
    --crash_queue_count;
    crash_dialog_visible = true;
    crash_button_hovered = false;
}

void compositor_report_crash(const compositor_crash_report_t* report) {
    if (!report) return;

    u64 flags = 0;
    spin_lock_irqsave(&crash_queue_lock, &flags);
    if (!crash_dialog_visible) {
        active_crash = *report;
        active_crash.process_name[CRASH_PROCESS_NAME_MAX - 1] = '\0';
        crash_dialog_visible = true;
        crash_button_hovered = false;
    } else if (crash_queue_count < CRASH_QUEUE_CAPACITY) {
        u32 tail = (crash_queue_head + crash_queue_count) % CRASH_QUEUE_CAPACITY;
        crash_queue[tail] = *report;
        crash_queue[tail].process_name[CRASH_PROCESS_NAME_MAX - 1] = '\0';
        ++crash_queue_count;
    }
    spin_unlock_irqrestore(&crash_queue_lock, flags);

    compositor_invalidate();
}

static void dismiss_crash_dialog() {
    u64 flags = 0;
    spin_lock_irqsave(&crash_queue_lock, &flags);
    crash_promote_next_locked();
    spin_unlock_irqrestore(&crash_queue_lock, flags);
    compositor_invalidate();
}

static void render_crash_dialog(xenon_surface_t* s) {
    if (!s || !crash_dialog_visible) return;

    i32 x, y, w, h;
    get_crash_dialog_rect(&x, &y, &w, &h);

    // dim the desktop
    xenon_surface_rect(s, 0, 0, s->width, s->height, 0x99000000);
    round_rect(s, x, y, w, h, 14, CRASH_DIALOG_BORDER);
    round_rect(s, x + 1, y + 1, w - 2, h - 2, 13, CRASH_DIALOG_BG);

    xenon_surface_str(s, x + 22, y + 18, "Application crashed", 0xFFF87171);
    xenon_surface_str(s, x + 22, y + 42, active_crash.process_name[0] ? active_crash.process_name : "User process", CRASH_DIALOG_TEXT);

    char pid_text[32] = "PID: ";
    u32 n = 5;
    u64 pid = active_crash.pid;
    char digits[20];
    u32 digit_count = 0;
    do {
        digits[digit_count++] = (char)('0' + (pid % 10));
        pid /= 10;
    } while (pid && digit_count < sizeof(digits));
    while (digit_count && n < sizeof(pid_text) - 1) pid_text[n++] = digits[--digit_count];
    pid_text[n] = '\0';
    xenon_surface_str(s, x + 22, y + 62, pid_text, CRASH_DIALOG_MUTED);
    xenon_surface_str(s, x + 22, y + 82, crash_exception_name(active_crash.exception), CRASH_DIALOG_TEXT);

    crash_draw_hex(s, x + 22,  y + 112, "RIP:", active_crash.rip);
    crash_draw_hex(s, x + 292, y + 112, "ERR:", active_crash.error_code);
    crash_draw_hex(s, x + 22,  y + 132, "RAX:", active_crash.rax);
    crash_draw_hex(s, x + 292, y + 132, "RBX:", active_crash.rbx);
    crash_draw_hex(s, x + 22,  y + 152, "RCX:", active_crash.rcx);
    crash_draw_hex(s, x + 292, y + 152, "RDX:", active_crash.rdx);
    crash_draw_hex(s, x + 22,  y + 172, "RSI:", active_crash.rsi);
    crash_draw_hex(s, x + 292, y + 172, "RDI:", active_crash.rdi);
    crash_draw_hex(s, x + 22,  y + 192, "RBP:", active_crash.rbp);
    crash_draw_hex(s, x + 292, y + 192, "RFLAGS:", active_crash.rflags);
    crash_draw_hex(s, x + 22,  y + 212, "R8:", active_crash.r8);
    crash_draw_hex(s, x + 292, y + 212, "R9:", active_crash.r9);
    crash_draw_hex(s, x + 22,  y + 232, "R10:", active_crash.r10);
    crash_draw_hex(s, x + 292, y + 232, "R11:", active_crash.r11);
    crash_draw_hex(s, x + 22,  y + 252, "R12:", active_crash.r12);
    crash_draw_hex(s, x + 292, y + 252, "R13:", active_crash.r13);
    crash_draw_hex(s, x + 22,  y + 272, "R14:", active_crash.r14);
    crash_draw_hex(s, x + 292, y + 272, "R15:", active_crash.r15);

    const i32 button_x = x + w - 112;
    const i32 button_y = y + h - 42;
    round_rect(s, button_x, button_y, 88, 28, 7, crash_button_hovered ? CRASH_DIALOG_BUTTON_HOVER : CRASH_DIALOG_BUTTON);
    xenon_surface_str(s, button_x + 36, button_y + 10, "OK", CRASH_DIALOG_TEXT);
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
    mutex_lock(&wallpaper_mutex);

    if (desktop_wallpaper.pixels) {
        compositor_image_draw(target, &desktop_wallpaper, 0, 0, 0);
    }
    else {
        xenon_surface_rect(target, 0, 0, target->width, target->height, DESKTOP_BG);
        xenon_surface_str(target, 20, 20, "Xenon Desktop", TEXT_WHITE);
        xenon_surface_str(target, 20, 40, "Development build", TEXT_MUTED);
    }

    mutex_unlock(&wallpaper_mutex);

    taskbar_render(target);
}

b8 compositor_init(xenon_surface_t* surface) {
    if (!surface) { return false; }

    target = surface;
    cursor_x = (i32)(surface->width / 2);
    cursor_y = (i32)(surface->height / 2);
    previous_buttons = 0;
    dragging_id = -1;

    spin_lock_init(&surfaces_lock);
    spin_lock_init(&crash_queue_lock);
    mutex_init(&wallpaper_mutex);

    window_server_set_invalidate_callback(compositor_invalidate);
    clamp_cursor();
    __atomic_store_n(&frame_invalidated, 1, __ATOMIC_RELEASE);

    if (!launcher_scan_bin()) {
        return false;
    }

    return true;
}

// IMPORTANT NOTE: called from window server, must use atomics
void compositor_invalidate() { __atomic_store_n(&frame_invalidated, 1, __ATOMIC_RELEASE); }

void compositor_render() {
    if (__atomic_exchange_n(&wallpaper_queued, 0, __ATOMIC_ACQUIRE)) {
        compositor_set_wallpaper(g_new_wallpaper_path);
    }

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

    render_launcher(target);
    render_crash_dialog(target);
    taskbar_render_top_level(target);

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

    // crash dialog consumes all mouse input while visible
    u64 crash_flags = 0;
    spin_lock_irqsave(&crash_queue_lock, &crash_flags);
    b8 modal_crash_visible = crash_dialog_visible;
    spin_unlock_irqrestore(&crash_queue_lock, crash_flags);

    if (modal_crash_visible) {
        b8 over_ok = crash_ok_hit_test(cursor_x, cursor_y);

        crash_flags = 0;
        spin_lock_irqsave(&crash_queue_lock, &crash_flags);
        if (crash_dialog_visible) {
            crash_button_hovered = over_ok;
        }
        spin_unlock_irqrestore(&crash_queue_lock, crash_flags);

        if (left_down && !left_was_down && over_ok) {
            dismiss_crash_dialog();
        } else if (cursor_x != old_x || cursor_y != old_y || evt->buttons != previous_buttons || evt->wheel != 0) {
            compositor_invalidate();
        }

        previous_buttons = evt->buttons;
        dragging_id = -1;
        return;
    }

    // TODO: consider passing struct pointer for reusability
    taskbar_process_mouse_events(target, cursor_x, cursor_y);

    if (left_down && !left_was_down) {
        if (!taskbar_handle_click(target, cursor_x, cursor_y)) {
            snapshot_count = window_server_snapshot(snapshots, COMPOSITOR_MAX_WINDOWS);

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

                    if (cursor_y >= wy && cursor_y < wy + TITLEBAR_H) {
                        dragging_id = (i32)hit->id;
                        drag_offset_x = cursor_x - wx;
                        drag_offset_y = cursor_y - wy;
                        window_server_set_focused(hit->handle);
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

    memset(pixels, 0, bytes);

    u64 flags = 0;
    spin_lock_irqsave(&surfaces_lock, &flags);

    surfaces[surfaces_count] = (window_surface_t) {
        .width = width,
        .height = height,
        .stride = (u32)stride,
        .format = WINDOW_PIXEL_FORMAT_RGBX8888,
        .handle = (window_surface_handle_t)surfaces_count, // TODO: better
        .pixels = pixels
    };

    __atomic_add_fetch(&surfaces[surfaces_count].refcount, 1, __ATOMIC_RELAXED);
    
    if (!window_server_set_surface(window, &surfaces[surfaces_count])) {
        surfaces[surfaces_count].pixels = NULL;
        spin_unlock_irqrestore(&surfaces_lock, flags);
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

    if (!compositor_image_scale_linear(&source, target->width, target->height, &scaled)) {
        compositor_image_destroy(&source);
        return false;
    }

    compositor_image_destroy(&source);

    // wallpapers are opaque, avoid per-pixel alpha blending
    size_t count = (size_t)scaled.width * scaled.height;

    for (size_t i = 0; i < count; ++i) {
        scaled.pixels[i] |= 0xFF000000;
    }

    mutex_lock(&wallpaper_mutex);

    compositor_image_t old = desktop_wallpaper;
    desktop_wallpaper = scaled;

    mutex_unlock(&wallpaper_mutex);
    
    // replace the previous wallpaper only after successful loading
    // safe only if every reader uses the same mutex
    compositor_image_destroy(&old);

    compositor_invalidate();
    return true;
}

void compositor_queue_set_wallpaper(const char* path) {
    if (!path) { return; }

    strncpy(g_new_wallpaper_path, path, sizeof(g_new_wallpaper_path));
    __atomic_store_n(&wallpaper_queued, 1, __ATOMIC_RELEASE);
}

static b8 launcher_super_down() {
    return launcher_left_super_down || launcher_right_super_down;
}

static char launcher_key_to_ascii(u16 keycode, b8 shift) {
    if (keycode >= 0x04 && keycode <= 0x1D) {
        return (char)('a' + keycode - 0x04);
    }

    if (keycode >= 0x1E && keycode <= 0x26) {
        static const char normal[]  = "123456789";
        static const char shifted[] = "!@#$%^&*(";

        return shift ? shifted[keycode - 0x1E] : normal[keycode - 0x1E];
    }

    if (keycode == 0x27)
        return shift ? ')' : '0';

    switch (keycode) {
        case 0x2C: return ' ';
        case 0x2D: return shift ? '_' : '-';
        case 0x2E: return shift ? '+' : '=';
        case 0x2F: return shift ? '{' : '[';
        case 0x30: return shift ? '}' : ']';
        case 0x31: return shift ? '|' : '\\';
        case 0x33: return shift ? ':' : ';';
        case 0x34: return shift ? '"' : '\'';
        case 0x35: return shift ? '~' : '`';
        case 0x36: return shift ? '<' : ',';
        case 0x37: return shift ? '>' : '.';
        case 0x38: return shift ? '?' : '/';
        default:   return 0;
    }
}

static void launcher_launch_selected() {
    launcher_program_t* item = launcher_match_at(launcher_selected);

    if (!item) {
        launcher_status = "No matching programs";
        compositor_invalidate();
        return;
    }

    if (!launcher_spawn_callback) {
        launcher_status = "Process launch handler not installed";
        compositor_invalidate();
        return;
    }

    char path[LAUNCHER_PATH_MAX + 1];
    const char prefix[] = "/bin/";
    u32 pos = 0;

    for (u32 i = 0; i < sizeof(prefix) - 1; ++i) {
        path[pos++] = prefix[i];
    }

    for (u32 i = 0; item->name[i]; ++i) {
        path[pos++] = item->name[i];
    }

    path[pos] = '\0';

    char out_path[LAUNCHER_PATH_MAX + 1];

    vfs_resolve_path("/", path, out_path, sizeof(out_path));

    if (launcher_spawn_callback(path)) {
        launcher_open = false;
        launcher_status = NULL;
    } else {
        launcher_status = "Could not start program";
    }

    compositor_invalidate();
}

b8 compositor_handle_key_event(u16 keycode, b8 pressed) {
    if (keycode == HID_KEY_LEFT_GUI || keycode == HID_KEY_RIGHT_GUI) {
        if (keycode == HID_KEY_LEFT_GUI) {
            launcher_left_super_down = pressed;
        }
        else {
            launcher_right_super_down = pressed;
        }

        if (keycode < 256) {
            launcher_eat_keyup[keycode] = pressed;
        }

        return true;
    }

    if (keycode == HID_KEY_LEFT_SHIFT || keycode == HID_KEY_RIGHT_SHIFT) {
        launcher_shift_down = pressed;

        if (launcher_open) {
            if (keycode < 256) {
                launcher_eat_keyup[keycode] = pressed;
            }

            return true;
        }

        if (!pressed && keycode < 256 && launcher_eat_keyup[keycode]) {
            launcher_eat_keyup[keycode] = false;
            return true;
        }

        return false;
    }

    // consume matching releases even if the overlay just closed
    if (!pressed && keycode < 256 &&
        launcher_eat_keyup[keycode]) {
        launcher_eat_keyup[keycode] = false;
        return true;
    }

    // win + s
    if (pressed && launcher_super_down() && keycode == HID_KEY_S) {
        if (!launcher_open) {
            launcher_open_search();
        }

        if (keycode < 256) {
            launcher_eat_keyup[keycode] = true;
        }

        return true;
    }

    if (!launcher_open)
        return false;

    // while open, the launcher owns keyboard input
    if (!pressed) {
        return true;
    }

    if (keycode < 256) {
        launcher_eat_keyup[keycode] = true;
    }

    switch (keycode) {
        case HID_KEY_ESCAPE: {
            launcher_open = false;
            launcher_status = NULL;
            compositor_invalidate();
            return true;
        }
        case HID_KEY_ENTER: {
            launcher_launch_selected();
            return true;
        }
        case HID_KEY_BACKSPACE: {
            if (launcher_query_len > 0) {
                launcher_query[--launcher_query_len] = '\0';
                launcher_selected = 0;
                launcher_status = NULL;
                compositor_invalidate();
            }
            return true;
        }
        case HID_KEY_UP_ARROW: {
            if (launcher_selected > 0) {
                --launcher_selected;
            }

            launcher_status = NULL;
            compositor_invalidate();
            return true;
        }
        case HID_KEY_DOWN_ARROW: {
            u32 count = launcher_match_count();

            if (count && launcher_selected + 1 < count) {
                ++launcher_selected;
            }

            launcher_status = NULL;
            compositor_invalidate();
            return true;
        }
        default: {
            break;
        }
    }

    char c = launcher_key_to_ascii(keycode, launcher_shift_down);

    if (c && launcher_query_len < LAUNCHER_QUERY_MAX) {
        launcher_query[launcher_query_len++] = c;
        launcher_query[launcher_query_len] = '\0';
        launcher_selected = 0;
        launcher_status = NULL;
        compositor_invalidate();
    }

    return true;
}

void compositor_set_program_spawn_fn(compositor_program_spawn_fn fn) {
    launcher_spawn_callback = fn;
}