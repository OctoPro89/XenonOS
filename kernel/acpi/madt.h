
#pragma once

#include <acpi/acpi.h>
#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>

#define MADT_DESCRIPTOR_TYPE_LAPIC                   0
#define MADT_DESCRIPTOR_TYPE_IOAPIC                  1
#define MADT_DESCRIPTOR_TYPE_IOAPIC_IRQ_SRC_OVERRIDE 2
#define MADT_DESCRIPTOR_TYPE_IOAPIC_NMI_SOURCE       3
#define MADT_DESCRIPTOR_TYPE_LAPIC_NMI               4
#define MADT_DESCRIPTOR_TYPE_LAPIC_ADDRESS_OVERRIDE  5
#define MADT_DESCRIPTOR_TYPE_PROCESSOR_LOCAL_X2APIC  9

#define LAPIC_PROCESSOR_ENABLED_BIT        (1u << 0)
#define LAPIC_PROCESSOR_ONLINE_CAPABLE_BIT  (1u << 1)

#define MADT_MAX_CPUS       256
#define MADT_MAX_IOAPICS    16
#define MADT_MAX_ISOS       48
#define MADT_MAX_NMI_SOURCES 16
#define MADT_MAX_LAPIC_NMIS 8

typedef struct __packed__ {
    acpi_sdt_header_t header;
    uint32_t lapic_address;
    uint32_t flags;
    uint8_t entries[];
} madt_table_t;

typedef struct __packed__ {
    uint8_t type;
    uint8_t length;
    uint8_t acpi_processor_id;
    uint8_t apic_id;
    uint32_t flags;
} lapic_desc_t;

typedef struct __packed__ {
    uint8_t type;
    uint8_t length;
    uint8_t ioapic_id;
    uint8_t reserved;
    uint32_t ioapic_address;
    uint32_t global_system_interrupt_base;
} ioapic_desc_t;

typedef struct __packed__ {
    uint8_t type;
    uint8_t length;
    uint8_t bus_source;
    uint8_t irq_source;
    uint32_t gsi;
    uint16_t flags;
} ioapic_irq_source_override_desc_t;

typedef struct __packed__ {
    uint8_t type;
    uint8_t length;
    uint8_t nmi_source;
    uint8_t reserved;
    uint16_t flags;
    uint32_t gsi;
} ioapic_nmi_source_desc_t;

typedef struct __packed__ {
    uint8_t type;
    uint8_t length;
    uint8_t apic_processor_id;
    uint16_t flags;
    uint8_t lint;
} lapic_nmi_desc_t;

typedef struct __packed__ {
    uint8_t type;
    uint8_t length;
    uint16_t reserved;
    uint64_t address;
} lapic_address_override_desc_t;

typedef struct __packed__ {
    uint8_t type;
    uint8_t length;
    uint16_t reserved;
    uint32_t x2apic_id;
    uint32_t flags;
    uint32_t acpi_id;
} lapic_x2apic_desc_t;

// parsed stuff, NOT ACPI records


typedef struct {
    uint32_t apic_id;
    uint8_t enabled;
} madt_lapic_info_t;

typedef struct {
    uint8_t id;
    uint32_t address;
    uint32_t global_irq_base;
} madt_ioapic_info_t;

typedef struct {
    uint8_t bus;
    uint8_t source;
    uint32_t gsi;
    uint16_t flags;
} madt_iso_info_t;

typedef struct {
    uint8_t source;
    uint16_t flags;
    uint32_t gsi;
} madt_nmi_source_info_t;

typedef struct {
    uint8_t processor_id;
    uint16_t flags;
    uint8_t lint;
} madt_lapic_nmi_info_t;

typedef struct {
    uint64_t lapic_base;

    madt_lapic_info_t lapics[MADT_MAX_CPUS];
    uint32_t lapic_count;

    madt_ioapic_info_t ioapics[MADT_MAX_IOAPICS];
    uint32_t ioapic_count;

    madt_iso_info_t isos[MADT_MAX_ISOS];
    uint32_t iso_count;

    madt_nmi_source_info_t nmi_sources[MADT_MAX_NMI_SOURCES];
    uint32_t nmi_source_count;

    madt_lapic_nmi_info_t lapic_nmis[MADT_MAX_LAPIC_NMIS];
    uint32_t lapic_nmi_count;
} madt_info_t;

__PRIVILEGED_CODE void madt_init(acpi_sdt_header_t* acpi_madt_table);

__PRIVILEGED_CODE const madt_info_t* madt_get_info();