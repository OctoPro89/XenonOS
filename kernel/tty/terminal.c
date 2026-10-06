#include <tty/terminal.h>
#include <drivers/input/input.h>

#include <memory/heap.h>
#include <xlibc/string.h>
#include <xlibc/stdio.h>
#include <errno.h>

void terminal_init(terminal_t* terminal);

void terminal_start_input_task(terminal_t* terminal);

ssize_t terminal_read(terminal_t* terminal, void* buffer, size_t size);
ssize_t terminal_write(terminal_t* terminal, const void* buffer, size_t size);
file_t* terminal_file_create(terminal_t* terminal);

static char keycode_to_ascii(u16 usage, b8 shift)
{
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

void terminal_init(terminal_t* terminal) {
    spin_lock_init(&terminal->lock);

    terminal->read_pos = 0;
    terminal->write_pos = 0;
    terminal->count = 0;

    terminal->line_start = 0;
    terminal->line_ready = false;

    wait_queue_init(&terminal->read_waiters);
}

static void terminal_push_char(
    terminal_t* terminal,
    char c
) {
    u64 flags;

    spin_lock_irqsave(
        &terminal->lock,
        &flags
    );

    if (terminal->count >= TERMINAL_BUFFER_SIZE) {
        spin_unlock_irqrestore(
            &terminal->lock,
            flags
        );

        return;
    }

    terminal->buffer[terminal->write_pos] = (u8)c;

    terminal->write_pos++;
    if (terminal->write_pos == TERMINAL_BUFFER_SIZE)
        terminal->write_pos = 0;

    terminal->count++;

    if (c == '\n')
        terminal->line_ready = true;

    spin_unlock_irqrestore(
        &terminal->lock,
        flags
    );

    wait_queue_wake_one(
        &terminal->read_waiters
    );
}

static void terminal_input_task(void* arg)
{
    terminal_t* terminal = arg;

    for (;;) {
        input_keyboard_event_t evt;

        if (!input_pop_keyboard_event(&evt)) {
            /*
             * No events right now. Sleep briefly for this first
             * implementation. We can make the input ring wakeable
             * properly next.
             */
            task_sleep_ms(1);
            continue;
        }

        if (evt.action != INPUT_KBD_ACTION_DOWN)
            continue;

        b8 shift = false; // TODO

        char c = keycode_to_ascii(
            evt.usage,
            shift
        );

        if (!c)
            continue;

        /*
         * Echo input.
         */
        putc(c);

        terminal_push_char(
            terminal,
            c
        );
    }
}

void terminal_start_input_task(terminal_t* terminal)
{
    task_create(
        NULL,
        terminal_input_task,
        terminal
    );
}

ssize_t terminal_read(
    terminal_t* terminal,
    void* buffer,
    size_t size
) {
    u8* out = buffer;

    if (!buffer && size != 0)
        return -EINVAL;

    if (size == 0)
        return 0;

    for (;;) {
        u64 flags;

        spin_lock_irqsave(
            &terminal->lock,
            &flags
        );

        if (terminal->line_ready) {
            size_t copied = 0;

            while (copied < size &&
                   terminal->count != 0) {

                u8 c = terminal->buffer[
                    terminal->read_pos
                ];

                terminal->read_pos++;

                if (terminal->read_pos ==
                    TERMINAL_BUFFER_SIZE) {

                    terminal->read_pos = 0;
                }

                terminal->count--;

                out[copied++] = c;

                if (c == '\n') {
                    terminal->line_ready = false;
                    break;
                }
            }

            spin_unlock_irqrestore(
                &terminal->lock,
                flags
            );

            return (ssize_t)copied;
        }

        /*
         * Atomically release the terminal lock and sleep.
         */
        task_wait(
            &terminal->read_waiters,
            &terminal->lock,
            flags
        );

        /*
         * task_wait() returns with the lock held.
         * Loop and inspect the condition again.
         */
        spin_unlock_irqrestore(
            &terminal->lock,
            flags
        );
    }
}

ssize_t terminal_write(
    terminal_t* terminal,
    const void* buffer,
    size_t size
) {
    const u8* data = buffer;

    (void)terminal;

    if (!buffer && size != 0)
        return -EINVAL;

    for (size_t i = 0; i < size; i++)
        putc((char)data[i]);

    return (ssize_t)size;
}

static ssize_t terminal_file_read(
    file_t* file,
    void* buffer,
    size_t size
) {
    terminal_t* terminal = file->private;

    return terminal_read(
        terminal,
        buffer,
        size
    );
}

static ssize_t terminal_file_write(
    file_t* file,
    const void* buffer,
    size_t size
) {
    terminal_t* terminal = file->private;

    return terminal_write(
        terminal,
        buffer,
        size
    );
}

static int terminal_file_close(file_t* file)
{
    (void)file;
    return 0;
}

static const file_ops_t terminal_file_ops = {
    .read  = terminal_file_read,
    .write = terminal_file_write,
    .seek  = NULL,
    .close = terminal_file_close,
};

file_t* terminal_file_create(
    terminal_t* terminal
) {
    file_t* file;

    file = kmalloc(sizeof(*file));

    if (!file)
        return NULL;

    file_init(
        file,
        &terminal_file_ops,
        terminal
    );

    return file;
}

/* drivers/tty/terminal.c */

static terminal_t g_console_terminal;

terminal_t* console_terminal()
{
    return &g_console_terminal;
}