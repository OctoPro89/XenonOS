#pragma once
#include <memory/memory_types.h>
#include <xlibc/xstdint.h>
#include <acpi/acpi.h>
#include <kernel.h>

// HPET Register Offsets
#define HPET_GENERAL_CAPABILITIES_ID_REGISTER   0x00
#define HPET_GENERAL_CONFIGURATION_OFFSET       0x10
#define HPET_MAIN_COUNTER_OFFSET                0xF0

// HPET General Configuration Register Bits
#define HPET_ENABLE_BIT     (1ULL << 0)   // Bit to enable HPET
#define HPET_64BIT_MODE_BIT (1ULL << 13)  // Bit 13: Enable 64-bit counter mode

typedef struct __packed__ {
    acpi_sdt_header_t header;
    uint8_t hardware_rev_id;
    uint8_t comparator_count : 5;
    uint8_t counter_size : 1;
    uint8_t reserved : 1;
    uint8_t legacy_replacement : 1;
    uint16_t pci_vendor_id;
    uint8_t address_space_id;
    uint8_t register_bit_width;
    uint8_t register_bit_offset;
    uint8_t reserved2;
    uint64_t address;
} hpet_table_t;

typedef struct {
    vaddr_t base;
} hpet_t;

__PRIVILEGED_CODE void hpet_init(hpet_t* hpet, acpi_sdt_header_t* acpi_hpet_table);
u64 hpet_read_counter(hpet_t* hpet);
u64 hpet_query_frequency(hpet_t* hpet);

extern hpet_t hpet_global;