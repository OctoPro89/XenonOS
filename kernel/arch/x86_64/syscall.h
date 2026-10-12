#pragma once

#include <kernel.h>

#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <io/fd.h>
#include <libwindow/window_client.h>

// TODO: maybe align to linux
// VERY IMPORTANT: IF THESE ARE CHANGED THEY MUST BE UPDATED IN unistd and libwindow
#define SYS_READ                        0
#define SYS_WRITE                       1
#define SYS_CLOSE                       2
#define SYS_EXIT                        3
#define SYS_SPAWN                       4
#define SYS_WAIT                        5
#define SYS_WINDOW_CREATE               6
#define SYS_WINDOW_DESTROY              7
#define SYS_WINDOW_SETUP_TERMINAL_IO    8

// NOTE: Must match stack pushes in arch/x86_64/syscall.asm
struct syscall_regs {
    u64 rax;
    u64 rbx;
    u64 rdx;
    u64 rsi;
    u64 rdi;
    u64 rbp;
    u64 r8;
    u64 r9;
    u64 r10;
    u64 r12;
    u64 r13;
    u64 r14;
    u64 r15;
};

typedef struct window_config window_config_t;

ssize_t sys_read(fd_t fd, void* buffer, size_t size);
ssize_t sys_write(fd_t fd, const void* buffer, size_t size);
int sys_close(fd_t fd);
__attribute__((noreturn)) void sys_exit(int code);
u64 sys_spawn(const char* user_path);
u64 sys_wait(u64 pid);
ssize_t sys_window_create(const window_config_t* user_config);
ssize_t sys_window_destroy();
ssize_t sys_window_damage();
ssize_t sys_window_setup_terminal_io();

u64 syscall_dispatch(struct syscall_regs* r);
void syscall_handler(struct syscall_regs* r);

extern void ASMCALL syscall_init();