#pragma once
#include <xlibc/xstdint.h>
#include <kernel.h>

extern ASMCALL void x86_64_restore_interrupt_context(u64 rsp) __attribute__((noreturn));