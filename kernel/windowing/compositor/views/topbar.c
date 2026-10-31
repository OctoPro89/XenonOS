#include <windowing/compositor/views/topbar.h>
#include <windowing/compositor/colors.h>
#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <xlibc/stdio.h>
#include <hw/rtc.h>

#define TOPBAR_HEIGHT 20

static const char* months[] = {
    "Jan", "Feb", "Mar", "Apr",
    "May", "Jun", "Jul", "Aug",
    "Sep", "Oct", "Nov", "Dec"
};

void topbar_render(xenon_surface_t* s) {
    if (!s) {
        return;
    }

    xenon_surface_rect(s, 0, 0, s->width, TOPBAR_HEIGHT, DESKTOP_PANEL);

    rtc_datetime_t dt;
    rtc_get_datetime(&dt);

    char datebuf[64];
    int written = snprintf(datebuf, 64, "%s %u %u:%02u\n", months[dt.month - 1], dt.day, dt.hour, dt.minute);
    
    xenon_surface_str(s, (u32)((s->width / 2) - (((u32)(written) * 8) / 2)), 8, datebuf, 0xFFFFFFFF);
}