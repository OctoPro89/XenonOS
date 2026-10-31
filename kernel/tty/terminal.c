#include <tty/terminal.h>
#include <drivers/input/input.h>
#include <windowing/window/window.h>
#include <windowing/window/internal.h>
#include <arch/x86_64/io.h>

#include <memory/heap.h>
#include <xlibc/string.h>
#include <xlibc/stdio.h>
#include <xlibc/errno.h>

#define HID_KEY_BACKSPACE 0x2A
#define HID_KEY_LEFT_SHIFT 0xE1
#define HID_KEY_RIGHT_SHIFT 0xE5

#define XETERM_COLOR_GREEN  0x0023D18B
#define XETERM_COLOR_TEXT   0x00CCCCCC
#define XETERM_COLOR_PATH   0x003883D8
#define XETERM_COLOR_BG     0x00191A1B

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

static u32 csi_param(terminal_t* terminal, u32 index, u32 fallback) {
    if (index >= terminal->csi_param_count ||
        terminal->csi_params[index] == 0) {
        return fallback;
    }

    return terminal->csi_params[index];
}

static u32 ansi_color(u32 color) {
    // ANSI order: black, red, green, yellow,
    // blue, magenta, cyan, white
    static const u32 colors[8] = {
        0x00000000,
        0x00AA0000,
        0x0000AA00,
        0x00AA5500,
        0x000000AA,
        0x00AA00AA,
        0x0000AAAA,
        0x00AAAAAA
    };

    return colors[color & 7];
}

static u32 terminal_ansi_to_rgb(u8 index) {
    static const u32 palette[16] = {
        0x00000000,
        0x00AA0000,
        0x0000AA00,
        0x00AA5500,
        0x000000AA,
        0x00AA00AA,
        0x0000AAAA,
        0x00AAAAAA,

        0x00555555,
        0x00FF5555,
        0x0055FF55,
        0x00FFFF55,
        0x005555FF,
        0x00FF55FF,
        0x0055FFFF,
        0x00FFFFFF 
    };

    return palette[index & 0x0F];
}

static void terminal_execute_csi( terminal_t* terminal, char command) {
    u32 p0 = csi_param(terminal, 0, 0);

    switch (command) {
        case 'J': {
            // ESC [ 2 J: erase the entire display
            if (p0 == 2 || p0 == 3) {
                terminal_clear(terminal, true);
            } else if (p0 == 0) {
                // erase from cursor to end of display
                for (u32 y = terminal->cursor_y; y < terminal->rows; ++y) {
                    u32 start = (y == terminal->cursor_y) ? terminal->cursor_x : 0;

                    for (u32 x = start; x < terminal->columns; ++x) {
                        terminal->cells[y][x].ch = ' ';
                    }
                }
            }
            break;
        }
        case 'H':
        case 'f': {
            // ESC [ row ; col H. Coordinates are 1-based
            u32 row = csi_param(terminal, 0, 1);
            u32 col = csi_param(terminal, 1, 1);

            if (row > terminal->rows) { row = terminal->rows; }
            if (col > terminal->columns) { col = terminal->columns; }

            terminal->cursor_y = row - 1;
            terminal->cursor_x = col - 1;
            break;
        }
        case 'A': {
            u32 n = csi_param(terminal, 0, 1);
            terminal->cursor_y = n > terminal->cursor_y ? 0 : terminal->cursor_y - n;
            break;
        }
        case 'B': {
            u32 n = csi_param(terminal, 0, 1);
            u32 max_y = terminal->rows - 1;
            terminal->cursor_y = n > max_y - terminal->cursor_y ? max_y : terminal->cursor_y + n;
            break;
        }
        case 'C': {
            u32 n = csi_param(terminal, 0, 1);
            u32 max_x = terminal->columns - 1;
            terminal->cursor_x = n > max_x - terminal->cursor_x ? max_x : terminal->cursor_x + n;
            break;
        }
        case 'D': {
            u32 n = csi_param(terminal, 0, 1);
            terminal->cursor_x = n > terminal->cursor_x ? 0 : terminal->cursor_x - n;
            break;
        }
        case 'm': {
            for (u32 i = 0; i < terminal->csi_param_count; ++i) {
                u32 p = terminal->csi_params[i];

                if (p == 0) {
                    terminal->foreground = 0x00AAAAAA;
                    terminal->background = 0x00000000;
                } else if (p >= 30 && p <= 37) {
                    terminal->foreground = terminal_ansi_to_rgb((u8)(p - 30));
                } else if (p >= 40 && p <= 47) {
                    terminal->background = terminal_ansi_to_rgb((u8)(p - 40));
                } else if (p >= 90 && p <= 97) {
                    terminal->foreground = terminal_ansi_to_rgb((u8)(p - 90 + 8));
                } else if (p >= 100 && p <= 107) {
                    terminal->background = terminal_ansi_to_rgb((u8)(p - 100 + 8));
                } else if (p == 39) {
                    terminal->foreground = 0x00AAAAAA;
                } else if (p == 49) {
                    terminal->background = 0x00000000;
                } else if ((p == 38 || p == 48) && i + 1 < terminal->csi_param_count) {
                    u32 mode = terminal->csi_params[i + 1];

                    if (mode == 2 && i + 4 < terminal->csi_param_count) {
                        u32 r = terminal->csi_params[i + 2];
                        u32 g = terminal->csi_params[i + 3];
                        u32 b = terminal->csi_params[i + 4];

                        if (r > 255) r = 255;
                        if (g > 255) g = 255;
                        if (b > 255) b = 255;

                        u32 rgb = (r << 16) | (g << 8) | b;

                        if (p == 38) {
                            terminal->foreground = rgb;
                        } else {
                            terminal->background = rgb;
                        }

                        i += 4;
                    } else if (mode == 5 && i + 2 < terminal->csi_param_count) {
                        u32 index = terminal->csi_params[i + 2];

                        if (p == 38) {
                            terminal->foreground = terminal_ansi_to_rgb((u8)index);
                        } else {
                            terminal->background = terminal_ansi_to_rgb((u8)index);
                        }

                        i += 2;
                    }
                }
            }
            break;
        }
        default: {
            // unsupported
            break;
        }
    }
}

static void terminal_render_grid(terminal_t* terminal, const window_surface_t* surface) {
    if (!terminal || !surface || !surface->pixels) {
        return;
    }

    window_surface_render_rect(surface, 0, 0, surface->width, surface->height, XETERM_COLOR_BG);

    for (u32 row = 0; row < terminal->rows; ++row) {
        for (u32 col = 0; col < terminal->columns; ++col) {
            terminal_cell_t* cell = &terminal->cells[row][col];

            i32 x = (i32)(col * TERMINAL_FONT_WIDTH);
            i32 y = (i32)(row * TERMINAL_FONT_HEIGHT);

            u32 fg = cell->fg_rgb;
            u32 bg = cell->bg_rgb;

            window_surface_render_rect(surface, x, y, TERMINAL_FONT_WIDTH, TERMINAL_FONT_HEIGHT, bg);

            if (cell->ch != ' ') {
                window_surface_render_char(surface, cell->ch, x, y, fg);
            }
        }
    }

    if (terminal->cursor_x < terminal->columns && terminal->cursor_y < terminal->rows) {

        u32 x = terminal->cursor_x * TERMINAL_FONT_WIDTH;
        u32 y = terminal->cursor_y * TERMINAL_FONT_HEIGHT;

        if (x + TERMINAL_FONT_WIDTH <= surface->width && y + TERMINAL_FONT_HEIGHT <= surface->height) {
            terminal_cell_t* cell = &terminal->cells[terminal->cursor_y][terminal->cursor_x];

            u32 fg = cell->fg_rgb;
            u32 bg = cell->bg_rgb;

            window_surface_render_rect(surface, (i32)x, (i32)y, TERMINAL_FONT_WIDTH, TERMINAL_FONT_HEIGHT, fg);

            if (cell->ch != ' ') {
                window_surface_render_char(surface, cell->ch, (i32)x, (i32)y, bg);
            }
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

    if (terminal->window && terminal->window->surface->pixels) {
        window_surface_render_rect(terminal->window->surface, 0, 0, terminal->window->surface->width, terminal->window->surface->height, terminal->background);
        terminal_render_grid(terminal, terminal->window->surface);

        // damage coordinates are local to the window not desktop
        window_server_damage(terminal->window, 0, 0, (i32)terminal->window->surface->width, (i32)terminal->window->surface->height);
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
    switch (c) {
        case '\n': {
            terminal_advance_line(terminal);
            break;
        }
        case '\r': {
            terminal->cursor_x = 0;
            break;
        }
        case '\b': {
            if (terminal->edit_length > 0 &&
                terminal->cursor_x > 0) {
                terminal->cursor_x--;

                terminal_cell_t* cell = &terminal->cells[terminal->cursor_y][terminal->cursor_x];

                cell->ch = c;
                cell->fg_rgb = terminal->foreground;
                cell->bg_rgb = terminal->background;
            }
            break;
        }
        case '\t': {
            terminal->cursor_x = (terminal->cursor_x + 4) & ~3u;

            if (terminal->cursor_x >= terminal->columns) {
                terminal_advance_line(terminal);
            }
            break;
        }
        default: {
            if ((u8)c >= 0x20) {
                if (terminal->cursor_x < terminal->columns &&
                    terminal->cursor_y < terminal->rows) {

                    terminal_cell_t* cell = &terminal->cells[terminal->cursor_y][terminal->cursor_x];

                    cell->ch = c;
                    cell->fg_rgb = terminal->foreground;
                    cell->bg_rgb = terminal->background;
                }

                terminal->cursor_x++;

                if (terminal->cursor_x >= terminal->columns) {
                    terminal_advance_line(terminal);
                }
            }
            break;
        }
    }
}

static void terminal_feed_char(terminal_t* terminal, char c) {
    switch (terminal->parse_state) {
        case TERMINAL_PARSE_NORMAL:
            if ((u8)c == 0x1B) {
                terminal->parse_state = TERMINAL_PARSE_ESC;
                return;
            }

            terminal_process_char(terminal, c);
            return;

        case TERMINAL_PARSE_ESC:
            if (c == '[') {
                terminal->parse_state = TERMINAL_PARSE_CSI;
                terminal->csi_param_count = 1;
                terminal->csi_have_value = false;

                for (u32 i = 0; i < 8; ++i) {
                    terminal->csi_params[i] = 0;
                }
            } else {
                terminal->parse_state = TERMINAL_PARSE_NORMAL;
            }
            return;

        case TERMINAL_PARSE_CSI:
            if (c >= '0' && c <= '9') {
                u32 i = terminal->csi_param_count - 1;
                u32 value = terminal->csi_params[i];

                if (value < 10000) {
                    value = value * 10 + (u32)(c - '0');
                    if (value > 10000) {
                        value = 10000;
                    }
                    terminal->csi_params[i] = (u16)value;
                }

                terminal->csi_have_value = true;
                return;
            }

            if (c == ';') {
                if (terminal->csi_param_count < 8) {
                    terminal->csi_param_count++;
                    terminal->csi_have_value = false;
                } else {
                    terminal->parse_state = TERMINAL_PARSE_NORMAL;
                }
                return;
            }

            if ((u8)c >= 0x40 && (u8)c <= 0x7E) {
                terminal_execute_csi(terminal, c);
            }

            if ((u8)c < 0x20 || (u8)c > 0x7E ||
                ((u8)c >= 0x40 && (u8)c <= 0x7E)) {
                terminal->parse_state = TERMINAL_PARSE_NORMAL;
            }
            return;
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

    terminal->edit_length = 0;

    terminal->columns = TERMINAL_COLUMNS;
    terminal->rows = TERMINAL_ROWS;

    terminal->foreground = XETERM_COLOR_TEXT;
    terminal->background = XETERM_COLOR_BG;

    terminal->parse_state = TERMINAL_PARSE_NORMAL;
    terminal->csi_param_count = 0;
    terminal->csi_have_value = false;

    for (u32 i = 0; i < 8; ++i) {
        terminal->csi_params[i] = 0;
    }

    for (u32 row = 0; row < terminal->rows; ++row) {
        for (u32 col = 0; col < terminal->columns; ++col) {
            terminal_cell_t* cell = &terminal->cells[row][col];

            cell->ch = ' ';
            cell->fg_rgb = terminal->foreground;
            cell->bg_rgb = terminal->background;
        }
    }
}

static void terminal_push_char(terminal_t* terminal, char c) {
    u64 flags;

    spin_lock_irqsave(&terminal->lock, &flags);

    if (c == '\b') {
        if (terminal->edit_length > 0 && terminal->count > 0) {
            if (terminal->write_pos == 0) {
                terminal->write_pos = TERMINAL_BUFFER_SIZE - 1;
            } else {
                terminal->write_pos--;
            }

            terminal->count--;
            terminal->edit_length--;
        }

        spin_unlock_irqrestore(&terminal->lock, flags);
        return;
    }

    if (terminal->count >= TERMINAL_BUFFER_SIZE) {
        spin_unlock_irqrestore(&terminal->lock, flags);
        return;
    }

    terminal->buffer[terminal->write_pos] = (u8)c;

    terminal->write_pos++;
    if (terminal->write_pos == TERMINAL_BUFFER_SIZE) {
        terminal->write_pos = 0;
    }

    terminal->count++;

    if (c == '\n') {
        terminal->line_ready = true;
        terminal->edit_length = 0;
    } else {
        terminal->edit_length++;
    }

    spin_unlock_irqrestore(&terminal->lock, flags);

    if (c == '\n') {
        wait_queue_wake_one(&terminal->read_waiters);
    }
}

// TODO: stop input task
static void terminal_input_task(void* arg) {
    terminal_t* terminal = arg;
    b8 left_shift = false;
    b8 right_shift = false;

    for (;;) {
        input_keyboard_event_t evt;

        if (!input_wait_pop_keyboard_event(&evt)) {
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

        if (c == '\b') {
            terminal_write(terminal, &c, 1);
            terminal_push_char(terminal, c);
        } else {
            terminal_push_char(terminal, c);
            terminal_write(terminal, &c, 1);
        }
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

        flags = task_wait(&terminal->read_waiters, &terminal->lock, flags);

        spin_unlock_irqrestore(&terminal->lock, flags);
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
        serial_write_char(data[i]);
        terminal_feed_char(terminal, data[i]);
    }

    // TODO: fix don't do in a lock
    if (terminal->window && terminal->window->surface) {
        window_surface_render_rect(terminal->window->surface, 0, 0, terminal->window->surface->width, terminal->window->surface->height, terminal->background);
        terminal_render_grid(terminal, terminal->window->surface);
        window_server_damage(terminal->window, 0, 0, terminal->window->surface->width, terminal->window->surface->height);
    }

    spin_unlock_irqrestore(&terminal->output_lock, flags);

    return (ssize_t)size;
}

void terminal_clear(terminal_t* terminal, b8 locked) {
    if (!terminal) {
        return;
    }

    u64 flags = 0;
    if (!locked) {
        spin_lock_irqsave(&terminal->output_lock, &flags);
    }

    for (u32 row = 0; row < terminal->rows; ++row) {
        for (u32 col = 0; col < terminal->columns; ++col) {
            terminal_cell_t* cell = &terminal->cells[row][col];
            cell->ch = ' ';
            cell->fg_rgb = terminal->foreground;
            cell->bg_rgb = terminal->background;
        }
    }

    terminal->cursor_x = 0;
    terminal->cursor_y = 0;

    if (terminal->window && terminal->window->surface && terminal->window->surface->pixels) {
        window_surface_render_rect(terminal->window->surface, 0, 0, terminal->window->surface->width, terminal->window->surface->height, terminal->background);
        terminal_render_grid(terminal, terminal->window->surface);
        window_server_damage(terminal->window, 0, 0, (i32)terminal->window->surface->width, (i32)terminal->window->surface->height);
    }

    if (!locked) {
        spin_unlock_irqrestore(&terminal->output_lock, flags);
    }
}

void terminal_attach_window(terminal_t* terminal, window_t* window) {
    if (!terminal || !window || !window->surface) {
        return;
    }

    terminal->window = window;

    terminal_render_grid(terminal, terminal->window->surface);
}

static terminal_t g_console_terminal;

terminal_t* console_terminal() {
    return &g_console_terminal;
}