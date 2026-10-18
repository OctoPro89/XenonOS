#include "doomkeys.h"
#include "doomgeneric.h"
#include <xlibc/ctype.h>
#include <xlibc/stdio.h>
#include <xlibc/string.h>
#include <unistd.h>
#include <time/time.h>
#include <libwindow/window_client.h>
#include <windowing/window/windefs.h>

#define KEYQUEUE_SIZE 16

static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int s_KeyQueueWriteIndex = 0;
static unsigned int s_KeyQueueReadIndex = 0;

#define HID_KEY_ENTER 0x28
#define HID_KEY_ESCAPE 0x29
#define HID_KEY_BACKSPACE 0x2A
#define HID_KEY_LEFT_SHIFT 0xE1
#define HID_KEY_RIGHT_SHIFT 0xE5
#define HID_KEY_LEFT_CTRL 0xE0
#define HID_KEY_RIGHT_CTRL 0xE4
#define HID_KEY_SPACE 0x2C
#define HID_KEY_LEFT_ARROW 0x50
#define HID_KEY_RIGHT_ARROW 0x4F
#define HID_KEY_UP_ARROW 0x52
#define HID_KEY_DOWN_ARROW 0x51

static char keycode_to_ascii(u16 usage, b8 shift) {
    switch (usage) {
        /* Letters */
        case 0x04: return shift ? 'A' : 'a';
        case 0x05: return shift ? 'B' : 'b';
        case 0x06: return shift ? 'C' : 'c';
        case 0x07: return shift ? 'D' : 'd';
        case 0x08: return shift ? 'E' : 'e';
        case 0x09: return shift ? 'F' : 'f';
        case 0x0A: return shift ? 'G' : 'g';
        case 0x0B: return shift ? 'H' : 'h';
        case 0x0C: return shift ? 'I' : 'i';
        case 0x0D: return shift ? 'J' : 'j';
        case 0x0E: return shift ? 'K' : 'k';
        case 0x0F: return shift ? 'L' : 'l';
        case 0x10: return shift ? 'M' : 'm';
        case 0x11: return shift ? 'N' : 'n';
        case 0x12: return shift ? 'O' : 'o';
        case 0x13: return shift ? 'P' : 'p';
        case 0x14: return shift ? 'Q' : 'q';
        case 0x15: return shift ? 'R' : 'r';
        case 0x16: return shift ? 'S' : 's';
        case 0x17: return shift ? 'T' : 't';
        case 0x18: return shift ? 'U' : 'u';
        case 0x19: return shift ? 'V' : 'v';
        case 0x1A: return shift ? 'W' : 'w';
        case 0x1B: return shift ? 'X' : 'x';
        case 0x1C: return shift ? 'Y' : 'y';
        case 0x1D: return shift ? 'Z' : 'z';

        /* Numbers */
        case 0x1E: return shift ? '!' : '1';
        case 0x1F: return shift ? '@' : '2';
        case 0x20: return shift ? '#' : '3';
        case 0x21: return shift ? '$' : '4';
        case 0x22: return shift ? '%' : '5';
        case 0x23: return shift ? '^' : '6';
        case 0x24: return shift ? '&' : '7';
        case 0x25: return shift ? '*' : '8';
        case 0x26: return shift ? '(' : '9';
        case 0x27: return shift ? ')' : '0';

        case 0x28:
            return '\n';

        case 0x2B:
            return '\t';

        case 0x2C:
            return ' ';

        case 0x2D:
            return shift ? '_' : '-';

        case 0x2E:
            return shift ? '+' : '=';

        case 0x2F:
            return shift ? '{' : '[';

        case 0x30:
            return shift ? '}' : ']';

        case 0x31:
            return shift ? '|' : '\\';

        case 0x33:
            return shift ? ':' : ';';

        case 0x34:
            return shift ? '"' : '\'';

        case 0x35:
            return shift ? '~' : '`';

        case 0x36:
            return shift ? '<' : ',';

        case 0x37:
            return shift ? '>' : '.';

        case 0x38:
            return shift ? '?' : '/';

        default:
            return 0;
    }
}

static void addKeyToQueue(int pressed, unsigned char key) {
	unsigned short keyData = (pressed << 8) | key;

	s_KeyQueue[s_KeyQueueWriteIndex] = keyData;
	s_KeyQueueWriteIndex++;
	s_KeyQueueWriteIndex %= KEYQUEUE_SIZE;
}

void DG_Init() {
    // create window
    window_config_t wconfig = {
        .title = "DOOM",
        .rect = {
            .x = 200,
            .y = 200,
            .width = DOOMGENERIC_RESX + 2,
            .height = DOOMGENERIC_RESY + 31
        },
        .type = WINDOW_TYPE_NORMAL,
        .flags = 0
    };

    ssize_t rc = window_create(&wconfig);
    if (rc < 0) {
        printf("Failed to create window! (rc: %d)\n", (int)rc);
        while (1) ;
    }

    rc = window_subscribe_keyboard_events();
    if (rc < 0) {
        printf("Failed to subscribe to keyboard events! (rc: %d)\n", (int)rc);
        while (1) ;
    }

	memset(s_KeyQueue, 0, KEYQUEUE_SIZE * sizeof(unsigned short));
}

int quit = 0;

void DG_DrawFrame() {
    window_event_t evt;
    while (window_get_event(&evt) >= 0) {
        switch (evt.type) {
            case WINDOW_EVENT_CLOSE_REQUEST: {
                // TODO: release resources
                quit = 1;
                break;
            }
            case WINDOW_EVENT_KEY_DOWN: {
                switch (evt.keycode) {
                    case HID_KEY_ENTER: {
                        addKeyToQueue(1, KEY_ENTER);
                        break;
                    }
                    case HID_KEY_ESCAPE: {
                        addKeyToQueue(1, KEY_ESCAPE);
                        break;
                    }
                    case HID_KEY_LEFT_SHIFT:
                    case HID_KEY_RIGHT_SHIFT: {
                        addKeyToQueue(1, KEY_RSHIFT);
                        break;
                    }
                    case HID_KEY_LEFT_CTRL:
                    case HID_KEY_RIGHT_CTRL: {
                        addKeyToQueue(1, KEY_FIRE);
                        break;
                    }
                    case HID_KEY_SPACE: {
                        addKeyToQueue(1, KEY_USE);
                        break;
                    }
                    case HID_KEY_LEFT_ARROW: {
                        addKeyToQueue(1, KEY_LEFTARROW);
                        break;
                    }
                    case HID_KEY_RIGHT_ARROW: {
                        addKeyToQueue(1, KEY_RIGHTARROW);
                        break;
                    }
                    case HID_KEY_DOWN_ARROW: {
                        addKeyToQueue(1, KEY_DOWNARROW);
                        break;
                    }
                    case HID_KEY_UP_ARROW: {
                        addKeyToQueue(1, KEY_UPARROW);
                        break;
                    }
                    default: {
                        addKeyToQueue(1, tolower(keycode_to_ascii(evt.keycode, false)));
                        break;
                    }
                }
                break;
            }
            case WINDOW_EVENT_KEY_UP: {
                switch (evt.keycode) {
                    case HID_KEY_ENTER: {
                        addKeyToQueue(0, KEY_ENTER);
                        break;
                    }
                    case HID_KEY_ESCAPE: {
                        addKeyToQueue(0, KEY_ESCAPE);
                        break;
                    }
                    case HID_KEY_LEFT_SHIFT:
                    case HID_KEY_RIGHT_SHIFT: {
                        addKeyToQueue(0, KEY_RSHIFT);
                        break;
                    }
                    case HID_KEY_LEFT_CTRL:
                    case HID_KEY_RIGHT_CTRL: {
                        addKeyToQueue(0, KEY_FIRE);
                        break;
                    }
                    case HID_KEY_SPACE: {
                        addKeyToQueue(0, KEY_USE);
                        break;
                    }
                    case HID_KEY_LEFT_ARROW: {
                        addKeyToQueue(0, KEY_LEFTARROW);
                        break;
                    }
                    case HID_KEY_RIGHT_ARROW: {
                        addKeyToQueue(0, KEY_RIGHTARROW);
                        break;
                    }
                    case HID_KEY_DOWN_ARROW: {
                        addKeyToQueue(0, KEY_DOWNARROW);
                        break;
                    }
                    case HID_KEY_UP_ARROW: {
                        addKeyToQueue(0, KEY_UPARROW);
                        break;
                    }
                    default: {
                        addKeyToQueue(0, tolower(keycode_to_ascii(evt.keycode, false)));
                        break;
                    }
                    break;
                }
                break;
            }
            default: break;
        }
    }

    window_memcpy_frame(DG_ScreenBuffer, DOOMGENERIC_RESX * DOOMGENERIC_RESY * sizeof(u32)); // TODO: better solution
    static const window_rect_t rect = { 0, 0, DOOMGENERIC_RESX, DOOMGENERIC_RESY };
    window_damage(&rect);

    static uint32_t last_time = 0;
    static uint32_t frames = 0;

    uint32_t now = DG_GetTicksMs();

    if (last_time == 0)
        last_time = now;

    frames++;

    uint32_t elapsed = now - last_time;

    if (elapsed >= 1000) {
        printf("FPS: %u\n", (frames * 1000) / elapsed);

        frames = 0;
        last_time = now;
    }
}

// TODO: real clock monotic sleep
#include <arch/x86_64/syscall.h>

void DG_SleepMs(uint32_t ms) {
    uint32_t ns = ms * 1000000;
    
    asm volatile (
        "syscall"
        :
        : "a"(SYS_SLEEP_NS),
          "D"(ns)
        : "rcx", "r11", "r10", "memory"
    );
}

#include <arch/x86_64/syscall.h>

uint32_t DG_GetTicksMs() {
    // TODO: clock monotic etc
    long ret;
    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_GET_TIME_MS)
        : "rcx", "r11", "r10", "memory"
    );

    return (u32)ret;
}

int DG_GetKey(int* pressed, unsigned char* doomKey) {
	if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex) {
		return 0;
	}
	else {
		unsigned short keyData = s_KeyQueue[s_KeyQueueReadIndex];
		s_KeyQueueReadIndex++;
		s_KeyQueueReadIndex %= KEYQUEUE_SIZE;

		*pressed = keyData >> 8;
		*doomKey = keyData & 0xFF;

		return 1;
	}
}

void DG_SetWindowTitle(const char* title) {
    // TODO:
}

int main(void) {
    printf("Loaded DOOM elf!\n");

    // TODO: args
    char arg[64];
    strncpy(arg, "doom", 64);
    doomgeneric_Create(1, (char**)&arg);

    while (!quit) {
        doomgeneric_Tick();
    }

    window_destroy();
    return 0;
}