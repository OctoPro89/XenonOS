#pragma once
#include <xlibc/xstdint.h>
#include <kernel.h>

extern ASMCALL void x86_64_context_switch(u64* old_rsp, u64 new_rsp);