#include <acpi/acpi.h>
#include <xlibc/xstddef.h>
#include <xlibc/xassert.h>
#include <xlibc/string.h>
#include <memory/paging.h>
#include <memory/vmm.h>
#include <time/time.h>
#include <acpi/hpet.h>
#include <acpi/madt.h>

__PRIVILEGED_CODE static b8 acpi_validate_checksum(void* table, size_t length) {
    uint8_t sum = 0;
    uint8_t* data = (uint8_t*)table;
    for (size_t i = 0; i < length; ++i) {
        sum += data[i];
    }
    return sum == 0;
}

// TODO: not sure about the paging flags here
__PRIVILEGED_CODE static void map_acpi_table(paddr_t table_address) {
    vaddr_t vtable_addr = phys_to_hhdm(table_address);
    
    // initially just one page has to be mapped for the table
    if (vmm_virt_to_phys(&kernel_space, vtable_addr) == 0) {
        vmm_map(&kernel_space, vtable_addr, table_address, PAGE_PRESENT | PAGE_WRITABLE);
    }

    acpi_sdt_header_t* table = (acpi_sdt_header_t*)(vtable_addr);

    // Calculate the starting and ending physical addresses
    u64 start_address = (u64)table_address;
    u64 end_address = (u64)table_address + table->length - 1;

    // Align start and end addresses to page boundaries
    u64 start_page = start_address & ~(PAGE_SIZE - 1);
    u64 end_page = end_address & ~(PAGE_SIZE - 1);

    // Traverse and map each page in the range
    for (u64 current_page = start_page; current_page <= end_page; current_page += PAGE_SIZE) {
        // Check if the page is already mapped
        paddr_t physical_address = vmm_virt_to_phys(&kernel_space, (vaddr_t)current_page);
        if (physical_address == 0) {
            // Map the page if it's not already mapped
            vmm_map(&kernel_space, phys_to_hhdm(current_page), current_page, PAGE_PRESENT | PAGE_WRITABLE);
        }
    }
}

__PRIVILEGED_CODE void acpi_enumerate_acpi_tables(void* rsdp) {
    xassert(rsdp, "Invalid rsdp!");
    
    // get xsdt address
    rsdp_descriptor_t* rsdp_desc = (rsdp_descriptor_t*)rsdp;
    xassert(rsdp_desc->revision == 2, "rsdp_desc->revision");
    xsdt_t* xsdt_table = (xsdt_t*)rsdp_desc->xsdt_address;

    // ensure that the xsdt table is actually mapped into the kernel's addressing space
    map_acpi_table(rsdp_desc->xsdt_address);

    // validate xsdt checksum
    xassert(acpi_validate_checksum(xsdt_table, xsdt_table->header.length), "XSDT failed checksum!");

    // enumerate tables
    size_t entry_count = (xsdt_table->header.length - sizeof(acpi_sdt_header_t)) / sizeof(u64);
    for (size_t i = 0; i < entry_count; ++i) {
        u64 table_address = xsdt_table->entries[i];
        map_acpi_table((paddr_t)table_address);

        acpi_sdt_header_t* table = (acpi_sdt_header_t*)table_address;

        // validate table checksum
        if (acpi_validate_checksum(table, table->length)) {
            char table_name[5] = "";
            memcpy(table_name, table->signature, 4);

            // handle specific table (i.e MADT FADT HPET)
            if (strcmp(table_name, "MCFG") == 0) {
                // TODO: PCI MCFG
            }
            else if (strcmp(table_name, "HPET") == 0) {
                // initialize hpet timer
                hpet_init(&hpet_global, table);

                // initialize kernel time
                ktimer_init();
            }
            else if (strcmp(table_name, "APIC") == 0) {
                // initialize MADT table
                madt_init(table);
            }
            // TODO: FACP
        }
    }
}