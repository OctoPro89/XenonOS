#pragma once
#include <acpi/acpi.h>

#define MADT_DESCRIPTOR_TYPE_LAPIC                      0
#define MADT_DESCRIPTOR_TYPE_IOAPIC                     1
#define MADT_DESCRIPTOR_TYPE_IOAPIC_IRQ_SRC_OVERRIDE    2
#define MADT_DESCRIPTOR_TYPE_IOAPIC_NMI_SOURCE          3
#define MADT_DESCRIPTOR_TYPE_LAPIC_NMI                  4
#define MADT_DESCRIPTOR_TYPE_LAPIC_ADDRESS_OVERRIDE     5
#define MADT_DESCRIPTOR_TYPE_PROCESSOR_LOCAL_X2APIC     9

//
// If flags bit 0 is set the CPU is able to be enabled,
// if it is not set you need to check bit 1. If that one
// is set, you can still enable it, but if it is not, the
// CPU can not be enabled and the OS should not try.
//
#define LAPIC_PROCESSOR_ENABLED_BIT         (1 << 0)
#define LAPIC_PROCESSOR_ONLINE_CAPABLE_BIT  (1 << 1)

typedef struct __packed__ {
    acpi_sdt_header_t header;
    uint32_t        lapic_address;
    uint32_t        flags;
    uint8_t         entries[];
} madt_table_t;

typedef struct __packed__ {
    uint8_t type;
    uint8_t length;
    uint8_t acpi_processor_id;
    uint8_t apic_id;
    uint32_t flags;
} lapic_desc_t;

typedef struct  __packed__ {
    uint8_t type;
    uint8_t length;
    uint8_t ioapic_id;
    uint8_t reserved;
    uint32_t ioapic_address; // This is the base address of the IOAPIC
    uint32_t global_system_interrupt_base;
} ioapic_desc_t;

typedef struct __packed__ {
    uint8_t type;
    uint8_t length;
    uint8_t bus_source;
    uint8_t irq_source;
    uint32_t gsi;   // Global System Interrupt
    uint16_t flags;
} ioapic_irq_source_override_desc_t;

typedef struct __packed__ {
    uint8_t type;
    uint8_t length;
    uint8_t nmi_source;
    uint8_t rsvd;
    uint16_t flags;
    uint32_t gsi;   // Global System Interrupt
} ioapic_nmi_source_desc_t;

typedef struct __packed__ {
    uint8_t type;
    uint8_t length;
    uint8_t apic_processor_id; // (0xFF means all processors)
    uint16_t flags;
    uint8_t lint; // LINT# (0 or 1)
} lapic_nmi_desc_t;

typedef struct __packed__ {
    uint8_t type;
    uint8_t length;
    uint16_t rsvd;
    uint64_t address;
} lapic_address_override_desc_t;

typedef struct __packed__ {
    uint8_t type;
    uint8_t length;
    uint16_t rsvd;
    uint32_t x2apic_id; // Processor's local x2APIC ID
    uint32_t flags;     // Same as the Local APIC flags
    uint32_t acpi_id;
} lapic_x2apic_desc_t;

__PRIVILEGED_CODE void madt_init(acpi_sdt_header_t* acpi_madt_table);

extern lapic_desc_t madt_local_apic;