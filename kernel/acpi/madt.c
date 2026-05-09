#include <acpi/madt.h>
#include <acpi/ioapic.h>

lapic_desc_t madt_local_apic;

__PRIVILEGED_CODE void madt_init(acpi_sdt_header_t* acpi_madt_table) {
    madt_table_t* table = (madt_table_t*)acpi_madt_table;

    // Print basic MADT table information

    uint8_t* entry = table->entries;
    uint8_t* table_end = (uint8_t*)(table) + table->header.length;

    while (entry < table_end) {
        uint8_t entry_type = *entry;
        uint8_t entry_length = *(entry + 1);

        switch (entry_type) {
            case MADT_DESCRIPTOR_TYPE_LAPIC: {
                // Check if maximum supported number of local apics has been tracked
                lapic_desc_t* desc = (lapic_desc_t*)entry;
                if (desc->flags & LAPIC_PROCESSOR_ENABLED_BIT) {
                    madt_local_apic = *desc;
                }
                break;
            }
            case MADT_DESCRIPTOR_TYPE_IOAPIC: {
                ioapic_desc_t* desc = (ioapic_desc_t*)entry;
                // #ifdef ARCH_X86_64
                ioapic_create(desc->ioapic_address, desc->global_system_interrupt_base);
                // #endif // ARCH_X86_64
                break;
            }
            default: {
                break;
            }
        }

        // Advance to the next entry
        entry += entry_length;
    }
}