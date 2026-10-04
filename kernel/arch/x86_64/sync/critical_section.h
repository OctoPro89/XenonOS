#pragma once

#include <xlibc/xstdint.h>

#define X86_EFLAGS_IF (1ULL << 9)

static inline u64 irq_save() {
    u64 flags;
    asm volatile(
        "pushfq\n"
        "pop %0\n"
        "cli"
        : "=r"(flags)
        :
        : "memory"
    );

    return flags;
}

static inline void irq_restore(u64 flags) {
    asm volatile(
        "push %0\n"
        "popfq"
        :
        : "r"(flags)
        : "memory", "cc"
    );
}

static inline b8 irq_was_enabled(u64 flags) {
    return (flags & X86_EFLAGS_IF) != 0;
}