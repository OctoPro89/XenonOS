
#include <acpi/madt.h>
#include <acpi/ioapic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static madt_info_t g_madt_info;
lapic_desc_t madt_local_apic;

__PRIVILEGED_CODE const madt_info_t* madt_get_info() {
    return &g_madt_info;
}

__PRIVILEGED_CODE void madt_init(acpi_sdt_header_t* acpi_madt_table) {
    uint8_t* entry;
    uint8_t* table_end;
    madt_table_t* table;

    uint32_t x2apic_ids[MADT_MAX_CPUS];
    uint32_t x2apic_acpi_ids[MADT_MAX_CPUS];
    uint32_t x2apic_count = 0;
    uint32_t i;

    bool has_legacy_lapics;

    // Clear previous parsed data.
    g_madt_info = (madt_info_t){0};
    madt_local_apic = (lapic_desc_t){0};

    if (acpi_madt_table == NULL) {
        return;
    }

    table = (madt_table_t*)acpi_madt_table;

    if (table->header.length < sizeof(madt_table_t)) {
        return;
    }

    g_madt_info.lapic_base = table->lapic_address;

    entry = table->entries;
    table_end = (uint8_t*)table + table->header.length;

    while (entry <= table_end &&  (size_t)(table_end - entry) >= 2) {
        uint8_t type = entry[0];
        uint8_t length = entry[1];

        if (length < 2 ||(size_t)(table_end - entry) < length) {
            break;
        }

        switch (type) {
            case MADT_DESCRIPTOR_TYPE_LAPIC: {
                lapic_desc_t* desc;

                if (length < sizeof(lapic_desc_t)) {
                    break;
                }

                desc = (lapic_desc_t*)entry;

                if (!(desc->flags & LAPIC_PROCESSOR_ENABLED_BIT)) {
                    break;
                }

                if (g_madt_info.lapic_count < MADT_MAX_CPUS) {
                    madt_lapic_info_t* lapic;

                    lapic = &g_madt_info.lapics[g_madt_info.lapic_count++];

                    lapic->apic_id = desc->apic_id;
                    lapic->enabled = 1;

                    if (g_madt_info.lapic_count == 1) {
                        madt_local_apic = *desc;
                    }
                }
                break;
            }

        case MADT_DESCRIPTOR_TYPE_IOAPIC: {
            ioapic_desc_t* desc;
            if (length < sizeof(ioapic_desc_t)) {
                break;
            }

            desc = (ioapic_desc_t*)entry;
            if (g_madt_info.ioapic_count < MADT_MAX_IOAPICS) {
                madt_ioapic_info_t* io;

                io = &g_madt_info.ioapics[
                    g_madt_info.ioapic_count++
                ];

                io->id = desc->ioapic_id;
                io->address = desc->ioapic_address;
                io->global_irq_base =
                    desc->global_system_interrupt_base;
            }

            ioapic_create(desc->ioapic_address, desc->global_system_interrupt_base);
            break;
        }
        case MADT_DESCRIPTOR_TYPE_IOAPIC_IRQ_SRC_OVERRIDE: {
            ioapic_irq_source_override_desc_t* desc;
            if (length < sizeof(ioapic_irq_source_override_desc_t)) {
                break;
            }

            desc = (ioapic_irq_source_override_desc_t*)entry;
            if (g_madt_info.iso_count < MADT_MAX_ISOS) {
                madt_iso_info_t* iso;

                iso = &g_madt_info.isos[g_madt_info.iso_count++];
                iso->bus = desc->bus_source;
                iso->source = desc->irq_source;
                iso->gsi = desc->gsi;
                iso->flags = desc->flags;
            }
            break;
        }
        case MADT_DESCRIPTOR_TYPE_IOAPIC_NMI_SOURCE: {
            ioapic_nmi_source_desc_t* desc;
            if (length < sizeof(ioapic_nmi_source_desc_t)) {
                break;
            }

            desc = (ioapic_nmi_source_desc_t*)entry;
            if (g_madt_info.nmi_source_count < MADT_MAX_NMI_SOURCES) {
                madt_nmi_source_info_t* nmi;

                nmi = &g_madt_info.nmi_sources[
                    g_madt_info.nmi_source_count++
                ];

                nmi->source = desc->nmi_source;
                nmi->flags = desc->flags;
                nmi->gsi = desc->gsi;
            }
            break;
        }
        case MADT_DESCRIPTOR_TYPE_LAPIC_NMI: {
            lapic_nmi_desc_t* desc;
            if (length < sizeof(lapic_nmi_desc_t)) {
                break;
            }

            desc = (lapic_nmi_desc_t*)entry;

            if (g_madt_info.lapic_nmi_count < MADT_MAX_LAPIC_NMIS) {
                madt_lapic_nmi_info_t* nmi;

                nmi = &g_madt_info.lapic_nmis[g_madt_info.lapic_nmi_count++];
                nmi->processor_id = desc->apic_processor_id;
                nmi->flags = desc->flags;
                nmi->lint = desc->lint;
            }
            break;
        }
        case MADT_DESCRIPTOR_TYPE_LAPIC_ADDRESS_OVERRIDE: {
            lapic_address_override_desc_t* desc;
            if (length < sizeof(lapic_address_override_desc_t)) {
                break;
            }

            desc = (lapic_address_override_desc_t*)entry;
            g_madt_info.lapic_base = desc->address;
            break;
        }
        case MADT_DESCRIPTOR_TYPE_PROCESSOR_LOCAL_X2APIC: {
            lapic_x2apic_desc_t* desc;
            if (length < sizeof(lapic_x2apic_desc_t)) {
                break;
            }

            desc = (lapic_x2apic_desc_t*)entry;
            if (!(desc->flags & LAPIC_PROCESSOR_ENABLED_BIT)) {
                break;
            }

            if (x2apic_count < MADT_MAX_CPUS) {
                x2apic_ids[x2apic_count] = desc->x2apic_id;
                x2apic_acpi_ids[x2apic_count] = desc->acpi_id;
                x2apic_count++;
            }
            break;
        }

        default: {
            break;
        }
    }

        entry += length;
    }

    has_legacy_lapics = (g_madt_info.lapic_count > 0);

    for (i = 0; i < x2apic_count && g_madt_info.lapic_count < MADT_MAX_CPUS; ++i) {

        madt_lapic_info_t* lapic;

        if (has_legacy_lapics && x2apic_ids[i] < 0xFF) {
            continue;
        }

        lapic = &g_madt_info.lapics[g_madt_info.lapic_count++];

        lapic->apic_id = x2apic_ids[i];
        lapic->enabled = 1;
    }
}
