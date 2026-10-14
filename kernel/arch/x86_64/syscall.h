#pragma once

#include <kernel.h>

#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <io/fd.h>
#include <libwindow/window_client.h>
#include <windowing/window/events.h>
#include <filesystem/sys_dirent.h>

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
#define SYS_WINDOW_DAMAGE               8
#define SYS_WINDOW_MEMCPY_FRAME         9
#define SYS_WINDOW_POP_EVENT            10
#define SYS_WINDOW_SETUP_TERMINAL_IO    11
#define SYS_OPENDIR                     12
#define SYS_READDIR                     13
#define SYS_SBRK                        14
#define SYS_CHDIR                       15
#define SYS_GETCWD                      16
#define SYS_GET_TIME_MS                 17
// NOTE: userspace should just use SYS_CLOSE on directories it'll work fine

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
ssize_t sys_window_damage(const window_rect_t* rect);
ssize_t sys_window_memcpy_frame(const void* user_buffer, size_t size);
ssize_t sys_window_pop_event(window_event_t* evt);
ssize_t sys_window_setup_terminal_io();
ssize_t sys_opendir(const char* user_path);
ssize_t sys_readdir(fd_t fd, sys_dirent_t* user_entry);
ssize_t sys_sbrk(ssize_t increment);
ssize_t sys_chdir(const char* user_path);
ssize_t sys_getcwd(char* user_buffer, size_t size);
u64 sys_get_time_ms();

u64 syscall_dispatch(struct syscall_regs* r);
void syscall_handler(struct syscall_regs* r);

extern void ASMCALL syscall_init();