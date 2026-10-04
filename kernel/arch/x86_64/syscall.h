#pragma once

#include <kernel.h>

#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <io/fd.h>

#define SYS_READ   0
#define SYS_WRITE  1
#define SYS_CLOSE  2

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

ssize_t sys_read(fd_t fd, void* buffer, size_t size);
ssize_t sys_write(fd_t fd, const void* buffer, size_t size);
int sys_close(fd_t fd);

u64 syscall_dispatch(struct syscall_regs* r);
void syscall_handler(struct syscall_regs* r);

extern void ASMCALL syscall_init();