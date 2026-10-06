#pragma once

#include <io/file.h>
#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <arch/x86_64/sync/sync.h>

#define TERMINAL_BUFFER_SIZE 4096

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
} terminal_t;

void terminal_init(terminal_t* terminal);

void terminal_start_input_task(terminal_t* terminal);

ssize_t terminal_read(terminal_t* terminal, void* buffer, size_t size);
ssize_t terminal_write(terminal_t* terminal, const void* buffer, size_t size);
file_t* terminal_file_create(terminal_t* terminal);

terminal_t* console_terminal();