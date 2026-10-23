#pragma once

#include <io/file.h>
#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <arch/x86_64/sync/sync.h>

#define TERMINAL_BUFFER_SIZE 1024

#define TERMINAL_COLUMNS  59
#define TERMINAL_ROWS     35

typedef struct window window_t;

typedef struct {
    char ch;
    u32 fg_rgb; // 0x00RRGGBB
    u32 bg_rgb; // 0x00RRGGBB
} terminal_cell_t;

typedef enum {
    TERMINAL_PARSE_NORMAL,
    TERMINAL_PARSE_ESC,
    TERMINAL_PARSE_CSI
} terminal_parse_state_t;

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

    u32 cursor_x;
    u32 cursor_y;

    u32 columns;
    u32 rows;

    u32 foreground;
    u32 background;

    size_t edit_length;

    terminal_parse_state_t parse_state;

    u16 csi_params[8];
    u8 csi_param_count;
    b8 csi_have_value;

    terminal_cell_t cells[TERMINAL_ROWS][TERMINAL_COLUMNS];
} terminal_t;

void terminal_init(terminal_t* terminal);

void terminal_start_input_task(terminal_t* terminal);

ssize_t terminal_read(terminal_t* terminal, void* buffer, size_t size);
ssize_t terminal_write(terminal_t* terminal, const void* buffer, size_t size);

void terminal_clear(terminal_t* terminal, b8 locked);

void terminal_attach_window(terminal_t* terminal, window_t* window);

terminal_t* console_terminal();