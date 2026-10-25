#include <xlibc/xstdint.h>
#include <arch/x86_64/syscall.h>

extern int main(int argc, char** argv);

static u64 syscall1(u64 number, u64 arg1) {
    u64 result;

    asm volatile(
        "syscall"
        : "=a"(result)
        : "a"(number),
          "D"(arg1)
        : "rcx", "r11", "r10", "memory"
    );

    return result;
}

__attribute__((noreturn)) void exit_process(int status) {
    syscall1(SYS_EXIT, (u64)status);

    for (;;) {
        asm volatile("hlt");
    }
}

/*
__attribute__((noreturn)) void _start(int argc, char** argv) {
    int result = main(argc, argv);

    exit_process(result);

    __builtin_unreachable();
}
*/