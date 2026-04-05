#include "syscall.h"
#include <arch/x86_64/io.h>

u64 user_rsp_save;

// NOTE: Must match stack pushes in arch/x86_64/syscall.asm
struct syscall_regs {
    uint64_t rax;
    uint64_t rbx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t r8, r9, r10, r12, r13, r14, r15;
};

uint64_t sys_write(uint64_t ptr, uint64_t len) {
    const char* s = (const char*)ptr;

    for (uint64_t i = 0; i < len; i++) {
        serial_write_char(s[i]);
    }

    return len;
}

uint64_t syscall_dispatch(struct syscall_regs* r) {
    switch (r->rax) {
        case 1: // write
            return sys_write(r->rdi, r->rsi);

        default:
            return (uint64_t)-1;
    }
}

void syscall_handler(struct syscall_regs* r) {
    r->rax = syscall_dispatch(r);
}