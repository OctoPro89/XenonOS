#pragma once

#include <xlibc/xstdint.h>

static inline void memset_fast_qword(void* dest, uint64_t value, uint64_t qwords) {
    asm volatile (
        "rep stosq"
        : "=D"(dest), "=c"(qwords)
        : "0"(dest), "1"(qwords), "a"(value)
        : "memory"
    );
}

static inline void memcpy_fast_qword(void* dest, const void* src, uint64_t qwords) {
    asm volatile (
        "rep movsq"
        : "=D"(dest), "=S"(src), "=c"(qwords)
        : "0"(dest), "1"(src), "2"(qwords)
        : "memory"
    );
}