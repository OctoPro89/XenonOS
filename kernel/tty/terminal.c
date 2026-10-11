#include <tty/terminal.h>
#include <drivers/input/input.h>
#include <windowing/window/window.h>
#include <arch/x86_64/io.h>

#include <memory/heap.h>
#include <xlibc/string.h>
#include <xlibc/stdio.h>
#include <errno.h>

#define HID_KEY_BACKSPACE 0x2A
#define HID_KEY_LEFT_SHIFT 0xE1
#define HID_KEY_RIGHT_SHIFT 0xE5

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

#define TERMINAL_FONT_WIDTH 8
#define TERMINAL_FONT_HEIGHT 8

static void terminal_render_grid(terminal_t* terminal, window_surface_t* surface) {
    if (!terminal || !surface || !surface->pixels) {
        return;
    }

    for (u32 row = 0; row < terminal->rows; ++row) {
        for (u32 col = 0; col < terminal->columns; ++col) {
            char c = terminal->cells[row][col].ch;
            if (c == ' ') { continue; }
            window_surface_render_char(surface, c, (i32)(col * TERMINAL_FONT_WIDTH), (i32)(row * TERMINAL_FONT_HEIGHT), terminal->foreground/* TODO: , terminal->background */);
        }
    }
}

static void terminal_scroll_up(terminal_t* terminal) {
    if (!terminal || terminal->rows == 0 || terminal->columns == 0) {
        return;
    }

    for (u32 row = 1; row < terminal->rows; ++row) {
        for (u32 col = 0; col < terminal->columns; ++col) {
            terminal->cells[row - 1][col] = terminal->cells[row][col];
        }
    }

    u32 last_row = terminal->rows - 1;

    for (u32 col = 0; col < terminal->columns; ++col) {
        terminal->cells[last_row][col].ch = ' ';
    }

    terminal->cursor_y = last_row;

    if (terminal->window && terminal->surface.pixels) {
        window_surface_render_rect(&terminal->surface, 0, 0, terminal->surface.width, terminal->surface.height, 0xFF101722);
        terminal_render_grid(terminal, &terminal->surface);

        // damage coordinates are local to the window not desktop
        window_damage(terminal->window, 0, 0, (i32)terminal->surface.width, (i32)terminal->surface.height);
    }
}

static void terminal_advance_line(terminal_t* terminal) {
    terminal->cursor_x = 0;

    if (terminal->cursor_y + 1 < terminal->rows) {
        terminal->cursor_y++;
    } else {
        terminal_scroll_up(terminal);
    }
}

static void terminal_process_char(terminal_t* terminal, char c) {
    serial_write_char(c);
    switch (c) {
        case '\n':
            terminal_advance_line(terminal);
            break;

        case '\r':
            terminal->cursor_x = 0;
            break;

        case '\b':
            if (terminal->cursor_x > 0) {
                terminal->cursor_x--;

                terminal->cells[terminal->cursor_y]
                            [terminal->cursor_x].ch = ' ';
            }
            break;

        case '\t':
            terminal->cursor_x = (terminal->cursor_x + 4) & ~3u;

            if (terminal->cursor_x >= terminal->columns) {
                terminal_advance_line(terminal);
            }
            break;
        default:
            if ((u8)c >= 0x20) {
                if (terminal->cursor_x < terminal->columns &&
                    terminal->cursor_y < terminal->rows) {

                    terminal->cells[terminal->cursor_y]
                                [terminal->cursor_x].ch = c;
                }

                terminal->cursor_x++;

                if (terminal->cursor_x >= terminal->columns) {
                    terminal_advance_line(terminal);
                }
            }
            break;
    }
}

void terminal_init(terminal_t* terminal) {
    if (!terminal) {
        return;
    }

    spin_lock_init(&terminal->lock);
    spin_lock_init(&terminal->output_lock);

    terminal->read_pos = 0;
    terminal->write_pos = 0;
    terminal->count = 0;
    terminal->line_start = 0;
    terminal->line_ready = false;

    wait_queue_init(&terminal->read_waiters);

    terminal->window = NULL;
    terminal->cursor_x = 0;
    terminal->cursor_y = 0;

    terminal->columns = TERMINAL_COLUMNS;
    terminal->rows = TERMINAL_ROWS;

    terminal->foreground = 0x00FFFFFF;
    terminal->background = 0x00101820;

    for (u32 row = 0; row < terminal->rows; ++row) {
        for (u32 col = 0; col < terminal->columns; ++col) {
            terminal->cells[row][col].ch = ' ';
        }
    }
}

static void terminal_push_char(terminal_t* terminal, char c) {
    u64 flags;

    spin_lock_irqsave(&terminal->lock, &flags);

    if (terminal->count >= TERMINAL_BUFFER_SIZE) {
        spin_unlock_irqrestore(&terminal->lock, flags);
        return;
    }

    terminal->buffer[terminal->write_pos] = (u8)c;

    ++terminal->write_pos;
    if (terminal->write_pos == TERMINAL_BUFFER_SIZE) {
        terminal->write_pos = 0;
    }

    ++terminal->count;

    if (c == '\n') {
        terminal->line_ready = true;
    }

    spin_unlock_irqrestore(&terminal->lock, flags);

    wait_queue_wake_one(&terminal->read_waiters);
}

static void terminal_input_task(void* arg) {
    terminal_t* terminal = arg;
    b8 left_shift = false;
    b8 right_shift = false;

    for (;;) {
        input_keyboard_event_t evt;

        if (!input_pop_keyboard_event(&evt)) {
            task_yield();
            continue;
        }

        b8 down = evt.action == INPUT_KBD_ACTION_DOWN;

        if (evt.usage == HID_KEY_LEFT_SHIFT) {
            left_shift = down;
            continue;
        }

        if (evt.usage == HID_KEY_RIGHT_SHIFT) {
            right_shift = down;
            continue;
        }

        if (!down) {
            continue;
        }

        b8 shift = left_shift || right_shift;
        char c;

        if (evt.usage == HID_KEY_BACKSPACE) {
            c = '\b';
        } else {
            c = keycode_to_ascii(evt.usage, shift);
        }

        if (!c) {
            continue;
        }

        terminal_push_char(terminal, c);
        terminal_write(terminal, &c, 1);
    }
}

void terminal_start_input_task(terminal_t* terminal) {
    task_create(NULL, terminal_input_task, terminal);
}

ssize_t terminal_read(terminal_t* terminal, void* buffer, size_t size) {
    u8* out = buffer;

    if (!buffer && size != 0) {
        return -EINVAL;
    }

    if (size == 0) {
        return 0;
    }

    for (;;) {
        u64 flags;

        spin_lock_irqsave(&terminal->lock, &flags);

        if (terminal->line_ready) {
            size_t copied = 0;

            while (copied < size && terminal->count != 0) {
                u8 c = terminal->buffer[terminal->read_pos];
                ++terminal->read_pos;

                if (terminal->read_pos == TERMINAL_BUFFER_SIZE) {

                    terminal->read_pos = 0;
                }

                --terminal->count;

                out[copied++] = c;

                if (c == '\n') {
                    terminal->line_ready = false;
                    break;
                }
            }

            spin_unlock_irqrestore(&terminal->lock, flags);

            return (ssize_t)copied;
        }

        task_wait(&terminal->read_waiters, &terminal->lock, flags);

        spin_unlock_irqrestore(&terminal->lock, flags );
    }
}

ssize_t terminal_write(terminal_t* terminal, const void* buffer, size_t size) {
    if (!terminal || (!buffer && size != 0)) {
        return -EINVAL;
    }

    if (size == 0) {
        return 0;
    }

    const char* data = buffer;
    u64 flags;

    spin_lock_irqsave(&terminal->output_lock, &flags);

    for (size_t i = 0; i < size; i++) {
        terminal_process_char(terminal, data[i]);
    }

    // TODO: fix don't do in a lock
    if (terminal->window && terminal->surface.pixels) {
        window_surface_render_rect(&terminal->surface, 0, 0, terminal->surface.width, terminal->surface.height, terminal->background);

        terminal_render_grid(terminal, &terminal->surface);

        window_damage(terminal->window, 0, 0, (i32)terminal->surface.width, (i32)terminal->surface.height);
    }

    spin_unlock_irqrestore(&terminal->output_lock, flags);

    return (ssize_t)size;
}

void terminal_attach_window(terminal_t* terminal, window_t* window) {
    if (!terminal || !window) {
        return;
    }

    window_rect_t rect = window_get_rect(window);

    if (rect.width <= 2 || rect.height <= 31) {
        return;
    }

    u32 width = (u32)rect.width - 2;
    u32 height = (u32)rect.height - 31;

    size_t stride = (size_t)width * sizeof(u32);
    size_t bytes = stride * height;

    u32* pixels = kmalloc(bytes);
    if (!pixels) {
        return;
    }

    for (size_t i = 0; i < bytes / sizeof(u32); ++i) {
        pixels[i] = terminal->background;
    }

    terminal->surface = (window_surface_t) {
        .width = width,
        .height = height,
        .stride = (u32)stride,
        .format = WINDOW_PIXEL_FORMAT_RGBX8888,
        .generation = 1,
        .pixels = pixels
    };

    terminal->window = window;
    terminal->surface.pixels = pixels;

    terminal_render_grid(terminal, &terminal->surface);

    if (!window_set_surface(window, &terminal->surface)) {
        terminal->window = NULL;
        terminal->surface.pixels = NULL;
        kfree(pixels);
        return;
    }

    window_damage(window, 0, 0, (i32)width, (i32)height);
}

static terminal_t g_console_terminal;

terminal_t* console_terminal() {
    return &g_console_terminal;
}