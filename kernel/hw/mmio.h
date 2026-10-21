#pragma once

#include <xlibc/xstdint.h>
#include <arch/x86_64/barrier.h>
#include <kernel.h>
#include <memory/paging.h>

static __hint_inline__ u8 mmio_read8(vaddr_t addr) {
    u8 val = *(volatile u8*)addr;
    barrier_io_read();
    return val;
}

static __hint_inline__ u16 mmio_read16(vaddr_t addr) {
    u16 val = *(volatile u16*)addr;
    barrier_io_read();
    return val;
}

static __hint_inline__ u32 mmio_read32(vaddr_t addr) {
    u32 val = *(volatile u32*)addr;
    barrier_io_read();
    return val;
}

static __hint_inline__ u64 mmio_read64(vaddr_t addr) {
    u64 val = *(volatile u64*)addr;
    barrier_io_read();
    return val;
}

static __hint_inline__ void mmio_write8(vaddr_t addr, u8 val) {
    barrier_io_write();
    *(volatile u8*)addr = val;
}

static __hint_inline__ void mmio_write16(vaddr_t addr, u16 val) {
    barrier_io_write();
    *(volatile u16*)addr = val;
}

static __hint_inline__ void mmio_write32(vaddr_t addr, u32 val) {
    barrier_io_write();
    *(volatile u32*)addr = val;
}

static __hint_inline__ void mmio_write64(vaddr_t addr, u64 val) {
    barrier_io_write();
    *(volatile u64*)addr = val;
}