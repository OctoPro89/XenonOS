/*
 * Xenon Hardware Monitor
 */

#include <stdint.h>
#include <stddef.h>
#include <xlibc/stdio.h>
#include <xlibc/string.h>
#include <libwindow/window_client.h>
#include <windowing/window/windefs.h>
#include <graphics/font8x8.h>
#include <arch/x86_64/syscall.h>
#include <time.h>

#define CONTENT_W       300u
#define CONTENT_H       300u
#define HISTORY_LEN     120u
#define SAMPLE_PERIOD_MS 1000u

#define WINDOW_X        200
#define WINDOW_Y        150

#define C_BG            0xFFF6F5F4u
#define C_CARD          0xFFFFFFFFu
#define C_BORDER        0xFFE5E2E5u
#define C_TEXT          0xFF241F31u
#define C_MUTED         0xFF77767Bu
#define C_ACCENT        0xFF9141ACu
#define C_ACCENT_SOFT   0xFFF3E8F7u
#define C_GRID          0xFFEAE7EBu
#define C_GREEN         0xFF2EC27Eu
#define C_RED            0xFFE01B24u

static u32 framebuffer[CONTENT_W * CONTENT_H];
static i32 history_temp_mc[HISTORY_LEN];
static u8 history_valid[HISTORY_LEN];
static u32 history_count;
static i32 latest_temp_mc;
static i32 latest_sample_ok;
static i32 running = 1;
static char cpu_brand[64] = "CPU name unavailable";
static struct timespec s_start_time;

static void fill_rect(i32 x, i32 y, i32 w, i32 h, u32 color) {
    i32 x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    i32 row, col;

    if (w <= 0 || h <= 0 || x1 <= 0 || y1 <= 0 ||
        x0 >= (i32)CONTENT_W || y0 >= (i32)CONTENT_H) {
        return;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (i32)CONTENT_W) x1 = (i32)CONTENT_W;
    if (y1 > (i32)CONTENT_H) y1 = (i32)CONTENT_H;

    for (row = y0; row < y1; ++row) {
        u32 *dst = &framebuffer[(u32)row * CONTENT_W + (u32)x0];
        for (col = x0; col < x1; ++col) {
            *dst++ = color;
        }
    }
}

static void draw_char_scaled(char c, i32 x, i32 y, u32 color, i32 scale) {
    const u8 *glyph;
    i32 row, col;

    if ((unsigned char)c > 127u || scale < 1) return;
    glyph = font8x8_basic[(u8)c];

    for (row = 0; row < 8; ++row) {
        u8 bits = glyph[row];
        for (col = 0; col < 8; ++col) {
            if (bits & (u8)(1u << col)) {
                fill_rect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

static void draw_text(const char *text, i32 x, i32 y, u32 color, i32 scale) {
    i32 start_x = x;
    if (!text) return;

    while (*text) {
        if (*text == '\n') {
            x = start_x;
            y += 8 * scale;
        } else {
            if (x + 8 * scale > (i32)CONTENT_W) break;
            draw_char_scaled(*text, x, y, color, scale);
            x += 8 * scale;
        }
        ++text;
    }
}

static void draw_hline(i32 x, i32 y, i32 w, u32 color) {
    fill_rect(x, y, w, 1, color);
}

static void draw_line(i32 x0, i32 y0, i32 x1, i32 y1, u32 color) {
    i32 dx = x1 >= x0 ? x1 - x0 : x0 - x1;
    i32 sx = x0 < x1 ? 1 : -1;
    i32 dy_abs = y1 >= y0 ? y1 - y0 : y0 - y1;
    i32 dy = -dy_abs;
    i32 sy = y0 < y1 ? 1 : -1;
    i32 err = dx + dy;

    for (;;) {
        fill_rect(x0, y0, 2, 2, color);
        if (x0 == x1 && y0 == y1) break;
        {
            i32 e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
}

static void draw_card(i32 x, i32 y, i32 w, i32 h) {
    fill_rect(x, y, w, h, C_CARD);
    draw_hline(x, y, w, C_BORDER);
    draw_hline(x, y + h - 1, w, C_BORDER);
    fill_rect(x, y, 1, h, C_BORDER);
    fill_rect(x + w - 1, y, 1, h, C_BORDER);
}

static void append_history(i32 ok, i32 temp_mc) {
    u32 i;

    if (history_count < HISTORY_LEN) {
        history_temp_mc[history_count] = temp_mc;
        history_valid[history_count] = (u8)(ok != 0);
        ++history_count;
        return;
    }

    for (i = 1; i < HISTORY_LEN; ++i) {
        history_temp_mc[i - 1] = history_temp_mc[i];
        history_valid[i - 1] = history_valid[i];
    }
    history_temp_mc[HISTORY_LEN - 1] = temp_mc;
    history_valid[HISTORY_LEN - 1] = (u8)(ok != 0);
}

static void append_unsigned(char **dst, u32 value) {
    char digits[12];
    u32 n = 0;
    do {
        digits[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value && n < sizeof(digits));
    while (n) *(*dst)++ = digits[--n];
}

static void format_cpu_count(char *out, u32 online, u32 total) {
    char *dst = out;

    append_unsigned(&dst, online);
    *dst++ = ' ';
    *dst++ = '/';
    *dst++ = ' ';
    append_unsigned(&dst, total);
    *dst = '\0';
}

static void format_temperature(char *out, i32 temp_mc) {
    i32 tenths = temp_mc >= 0 ? (temp_mc + 50) / 100 : (temp_mc - 50) / 100;
    u32 magnitude;
    char *dst = out;

    if (tenths < 0) {
        *dst++ = '-';
        magnitude = (u32)(-tenths);
    } else {
        magnitude = (u32)tenths;
    }

    append_unsigned(&dst, magnitude / 10u);
    *dst++ = '.';
    *dst++ = (char)('0' + (magnitude % 10u));
    *dst++ = ' ';
    *dst++ = 'C';
    *dst = '\0';
}

static i32 read_cpu_temperature_mc(i32 *out_mc) {
    long ret;
    if (!out_mc) return 0;

    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(SYS_HWMON_GET_CPU_TEMP_MC)
        : "rcx", "r11", "r10", "memory"
    );

    if (ret < 0 || ret > 150000) return 0;
    *out_mc = (i32)ret;
    return 1;
}

static int read_cpu_brand(char *out, size_t out_size) {
    cpu_info_t info = {0};
    long ret;

    if (!out || out_size == 0) {
        return 0;
    }

    out[0] = '\0';

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_GET_CPU_INFO), "D"(&info)
        : "rcx", "r11", "r10", "memory"
    );

    if (ret < 0) {
        return 0;
    }

    size_t i;
    for (i = 0; i + 1 < out_size && i < sizeof(info.brand); ++i) {
        unsigned char c = (unsigned char)info.brand[i];
        if (c == '\0') {
            break;
        }

        out[i] = (c >= 32 && c <= 126) ? (char)c : '?';
    }

    out[i] = '\0';
    return i != 0;
}

static u32 get_ticks_ms() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    u64 start_ms = (u64)s_start_time.tv_sec * 1000ULL + (u64)s_start_time.tv_nsec / 1000000ULL;
    u64 now_ms = (u64)now.tv_sec * 1000ULL + (u64)now.tv_nsec / 1000000ULL;

    return (u32)(now_ms - start_ms);
}

static void sleep_ms(u32 ms) {
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000);
    ts.tv_nsec = (long)((ms % 1000) * 1000000L);
    nanosleep(&ts, NULL);
}

static u64 get_online_cpu_count() {
    long ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(SYS_HWMON_GET_ONLINE_CPUS)
        : "rcx", "r11", "r10", "memory"
    );
    return (u64)ret;
}

static u64 get_total_cpu_count() {
    long ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(SYS_HWMON_GET_TOTAL_CPUS)
        : "rcx", "r11", "r10", "memory"
    );
    return (u64)ret;
}

static void render_temperature_graph() {
    const i32 gx = 23;
    const i32 gy = 214;
    const i32 gw = 254;
    const i32 gh = 34;
    i32 min_mc = 150000;
    i32 max_mc = 0;
    i32 have_value = 0;
    i32 span;
    u32 i;
    u32 x_denom;

    for (i = 0; i < history_count; ++i) {
        if (!history_valid[i]) continue;
        if (!have_value || history_temp_mc[i] < min_mc) min_mc = history_temp_mc[i];
        if (!have_value || history_temp_mc[i] > max_mc) max_mc = history_temp_mc[i];
        have_value = 1;
    }

    if (!have_value) {
        min_mc = 30000;
        max_mc = 90000;
    } else {
        if (max_mc - min_mc < 5000) {
            i32 center = (max_mc + min_mc) / 2;
            min_mc = center - 2500;
            max_mc = center + 2500;
        } else {
            i32 margin = (max_mc - min_mc) / 8;
            min_mc -= margin;
            max_mc += margin;
        }
        if (min_mc < 0) min_mc = 0;
        if (max_mc > 150000) max_mc = 150000;
        min_mc = (min_mc / 1000) * 1000;
        max_mc = ((max_mc + 999) / 1000) * 1000;
        if (max_mc <= min_mc) max_mc = min_mc + 5000;
    }
    span = max_mc - min_mc;
    if (span <= 0) span = 1;
    x_denom = (history_count < HISTORY_LEN) ?
              (history_count > 1 ? history_count - 1 : 1) : HISTORY_LEN - 1;

    for (i = 0; i < 4; ++i) {
        i32 y = gy + (i32)((gh - 1) * i / 3);
        draw_hline(gx, y, gw, C_GRID);
    }

    if (history_count == 0) {
        draw_text("Waiting for samples...", gx + 30, gy + 24, C_MUTED, 1);
        return;
    }

    for (i = 1; i < history_count; ++i) {
        i32 x0, x1, y0, y1;
        if (!history_valid[i - 1] || !history_valid[i]) continue;

        x0 = gx + (i32)((i - 1) * (u32)(gw - 1) / x_denom);
        x1 = gx + (i32)(i * (u32)(gw - 1) / x_denom);
        y0 = gy + gh - 1 - (i32)((history_temp_mc[i - 1] - min_mc) * (gh - 1) / span);
        y1 = gy + gh - 1 - (i32)((history_temp_mc[i] - min_mc) * (gh - 1) / span);
        if (y0 < gy) y0 = gy;
        if (y0 >= gy + gh) y0 = gy + gh - 1;
        if (y1 < gy) y1 = gy;
        if (y1 >= gy + gh) y1 = gy + gh - 1;
        draw_line(x0, y0, x1, y1, C_ACCENT);
    }

    if (history_count && history_valid[history_count - 1]) {
        u32 idx = history_count - 1;
        i32 x = gx + (i32)(idx * (u32)(gw - 1) / x_denom);
        i32 y = gy + gh - 1 - (i32)((history_temp_mc[idx] - min_mc) * (gh - 1) / span);
        if (y < gy) y = gy;
        if (y >= gy + gh) y = gy + gh - 1;
        fill_rect(x - 2, y - 2, 5, 5, C_ACCENT);
    }
}

static void render_dashboard() {
    char temperature_text[24];
    char cpu_count_text[24];
    const char *sensor_status;
    u32 online_cpus;
    u32 total_cpus;

    fill_rect(0, 0, CONTENT_W, CONTENT_H, C_BG);

    draw_text("Hardware", 16, 10, C_TEXT, 2);
    draw_text(cpu_brand, 17, 31, C_MUTED, 1);
    fill_rect(270, 17, 7, 7, latest_sample_ok ? C_GREEN : C_RED);

    /* CPU temperature card */
    draw_card(12, 49, 276, 75);
    fill_rect(283, 59, 3, 55, C_ACCENT);
    draw_text("CPU TEMPERATURE", 22, 57, C_MUTED, 1);

    if (latest_sample_ok) {
        format_temperature(temperature_text, latest_temp_mc);
        sensor_status = "TCTL / LIVE";
    } else {
        strcpy(temperature_text, "--.- C");
        sensor_status = "SENSOR UNAVAILABLE";
    }

    draw_text(temperature_text, 22, 73, C_TEXT, 2);
    draw_text(sensor_status, 174, 101, latest_sample_ok ? C_ACCENT : C_RED, 1);

    /* Symmetric multiprocessing status */
    draw_card(12, 132, 276, 45);
    draw_text("SMP", 22, 140, C_MUTED, 1);
    draw_text("Enabled", 22, 152, C_GREEN, 1);

    online_cpus = (u32)get_online_cpu_count();
    total_cpus = (u32)get_total_cpu_count();

    format_cpu_count(cpu_count_text, online_cpus, total_cpus);
    draw_text("Online CPUs:", 155, 140, C_MUTED, 1);
    draw_text(cpu_count_text, 155, 152, C_TEXT, 1);

    /* Temperature history */
    draw_card(12, 185, 276, 88);
    draw_text("TEMPERATURE HISTORY", 22, 191, C_TEXT, 1);
    draw_text("LAST 120 SECONDS", 22, 202, C_MUTED, 1);

    render_temperature_graph();

    draw_text("120s ago", 22, 254, C_MUTED, 1);
    draw_text("now", 248, 254, C_MUTED, 1);

    draw_hline(12, 280, 276, C_BORDER);

    if (latest_sample_ok) {
        draw_text("LIVE / SMN / 1 SEC", 16, 286, C_MUTED, 1);
    } else {
        draw_text("SENSOR READ FAILED", 16, 286, C_RED, 1);
    }
}

static void present_frame() {
    static const window_rect_t damaged = { 0, 0, CONTENT_W, CONTENT_H };
    window_memcpy_frame(framebuffer, sizeof(framebuffer));
    window_damage(&damaged);
}

static void sample_and_refresh() {
    i32 temperature_mc = 0;
    latest_sample_ok = read_cpu_temperature_mc(&temperature_mc);
    if (latest_sample_ok) latest_temp_mc = temperature_mc;

    append_history(latest_sample_ok, temperature_mc);
    render_dashboard();
    present_frame();
}

int main(void) {
    window_config_t config = {
        .title = "Hardware Monitor",
        .rect = {
            .x = WINDOW_X,
            .y = WINDOW_Y,
            .width = CONTENT_W + 2,
            .height = CONTENT_H + 31
        },
        .type = WINDOW_TYPE_NORMAL,
        .flags = 0
    };
    window_event_t event;
    ssize_t rc;
    u32 last_sample_ms;

    rc = window_create(&config);
    if (rc < 0) {
        printf("Hardware Monitor: window_create failed (rc=%d)\n", (i32)rc);
        return 1;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &s_start_time) != 0) {
        printf("Failed to initialize monotonic clock\n");
        while (1);
    }

    history_count = 0;
    latest_sample_ok = 0;
    latest_temp_mc = 0;
    if (!read_cpu_brand(cpu_brand, sizeof(cpu_brand))) {
        strcpy(cpu_brand, "CPU name unavailable");
    }
    render_dashboard();
    present_frame();

    last_sample_ms = get_ticks_ms() - SAMPLE_PERIOD_MS;

    while (running) {
        while (window_get_event(&event) >= 0) {
            if (event.type == WINDOW_EVENT_CLOSE_REQUEST) {
                running = 0;
                break;
            }
        }
        if (!running) break;

        {
            u32 now = get_ticks_ms();
            if ((u32)(now - last_sample_ms) >= SAMPLE_PERIOD_MS) {
                last_sample_ms = now;
                sample_and_refresh();
            }
        }

        sleep_ms(25);
    }

    window_destroy();
    return 0;
}