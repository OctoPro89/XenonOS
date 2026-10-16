#pragma once
#include <xlibc/xstdint.h>

typedef struct {
    char vendor[13];
    char brand[49];
    u32 family;
    u32 model;
    u32 stepping;
    
    // feature registers saved directly for userspace parsing
    u32 ecx_features;
    u32 edx_features;
    u32 ecx_leaf7;
} cpu_info_t;