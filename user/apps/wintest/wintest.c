#include <stdio.h>
#include <libwindow/window_client.h>
#include <arch/x86_64/syscall.h>
#include <stdlib.h>
#include <string.h>

#define HID_KEY_BACKSPACE 0x2A
#define HID_KEY_SPACE 0x2C

void sleep(u32 ms) {
    uint32_t ns = ms * 1000000;
    
    asm volatile (
        "syscall"
        :
        : "a"(SYS_SLEEP_NS),
          "D"(ns)
        : "rcx", "r11", "r10", "memory"
    );
}

int main(void) {
    printf("Loaded wintest!\nPress backspace to exit, space to copy frame.\n");

    u32* frame = malloc(200 * 200 * sizeof(u32));
    if (!frame) {
        printf("Failed to allocate frame!\n");
        goto end;
    }

    // straight white
    memset(frame, 0xFF, 200 * 200 * sizeof(u32));

    window_config_t wconfig = {
        .title = "wintest",
        .rect = { .x = 200, .y = 200, .width = 200 + 2, .height = 200 + 31 },
        .type = WINDOW_TYPE_NORMAL,
        .flags = 0
    };

    if (window_create(&wconfig) < 0) {
        printf("Failed to create window!\n");
        return -1;
    }

    if (window_subscribe_keyboard_events() < 0) {
        printf("Failed to get keyboard events!");
        return -1;
    }

    for (;;) {
        window_event_t evt;
        while (window_get_event(&evt) >= 0) {
            switch (evt.type) {
                case WINDOW_EVENT_KEY_DOWN: {
                    switch (evt.keycode) {
                        case HID_KEY_BACKSPACE: {
                            goto end;
                        }
                        case HID_KEY_SPACE: {
                            if (window_memcpy_frame(frame, 200 * 200 * sizeof(u32)) < 0) {
                                printf("window_memcpy_frame() failed!\n");
                            }
                            window_rect_t rect = {
                                .x = 0,
                                .y = 0,
                                .width = 200,
                                .height = 200
                            };
                            if (window_damage(&rect) < 0) {
                                printf("window_damage() failed\n");
                            }
                            break;
                        }
                        default:
                            break;
                    }
                    break;
                }
                case WINDOW_EVENT_CLOSE_REQUEST: {
                    goto end;
                }
                default:
                    break;
            }
        }
        sleep(2);
    }
    
end:
    printf("exiting\n");
    window_destroy();
    if (frame) { free(frame); }

    return 0;
}