#pragma once

#include <io/file.h>
#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <arch/x86_64/sync/sync.h>
#include <windowing/window/surface.h>

#define TERMINAL_BUFFER_SIZE 4096

#define TERMINAL_COLUMNS  100
#define TERMINAL_ROWS     30

typedef struct window window_t;

typedef struct {
    char ch;
    u32 foreground;
    u32 background;
} terminal_cell_t;

typedef struct terminal {
    spinlock_t lock;

    u8 buffer[TERMINAL_BUFFER_SIZE];

    size_t read_pos;
    size_t write_pos;

    size_t count;

    // canonical input: read() waits for '\n'
    size_t line_start;
    b8 line_ready;

    wait_queue_t read_waiters;

    spinlock_t output_lock;

    window_t* window;
    window_surface_t surface;

    u32 cursor_x;
    u32 cursor_y;

    u32 columns;
    u32 rows;

    u32 foreground;
    u32 background;

    terminal_cell_t cells[TERMINAL_ROWS][TERMINAL_COLUMNS];
} terminal_t;

void terminal_init(terminal_t* terminal);

void terminal_start_input_task(terminal_t* terminal);

ssize_t terminal_read(terminal_t* terminal, void* buffer, size_t size);
ssize_t terminal_write(terminal_t* terminal, const void* buffer, size_t size);

void terminal_attach_window(terminal_t* terminal, window_t* window);

terminal_t* console_terminal();