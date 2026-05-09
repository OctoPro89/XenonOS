#pragma once
#include <memory/memory_types.h>
#include <xlibc/xstdint.h>
#include <kernel.h>

#define IOAPICID          0x00
#define IOAPICVER         0x01
#define IOAPICARB         0x02
#define IOAPICREDTBL(n)   (0x10 + 2 * n) // lower-32bits (add +1 for upper 32-bits)

#define IOAPIC_REGSEL     0x00
#define IOAPIC_IOWIN      0x10

enum {
    IOAPIC_DELIVERY_MODE_EDGE  = 0,
    IOAPIC_DELIVERY_MODE_LEVEL = 1,
};

enum {
    IOAPIC_DESTINATION_MODE_PHYSICAL = 0,
    IOAPIC_DESTINATION_MODE_LOGICAL = 1,
};

typedef union {
    struct {
        uint64_t vector        : 8;
        uint64_t delv_mode     : 3;
        uint64_t dest_mode     : 1;
        uint64_t delv_status   : 1;
        uint64_t pin_polarity  : 1;
        uint64_t remote_irr    : 1;
        uint64_t trigger_mode  : 1;
        uint64_t mask          : 1;
        uint64_t reserved      : 39;
        uint64_t destination   : 8;
    };

    struct {
        uint32_t lower_dword;
        uint32_t upper_dword;
    };
} ioapic_redirection_entry_t;

typedef struct {
    paddr_t physical_base;
    vaddr_t virtual_base;
    u8 apic_id;
    u8 apic_version;
    u8 redirection_entry_count;
    u64 global_intr_base;
} ioapic_t;

__PRIVILEGED_CODE void ioapic_create(u64 physbase, u64 gsib);