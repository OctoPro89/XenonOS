#include <acpi/hpet.h>
#include <memory/paging.h>
#include <memory/vmm.h>

hpet_t hpet_global;

static __hint_inline__ u64 hpet_read_hpet_register(const hpet_t* hpet, u64 offset) {
    return *(volatile u64*)(hpet->base + offset);
}

static __hint_inline__ void hpet_write_hpet_register(hpet_t* hpet, u64 offset, u64 value) {
    *(volatile u64*)(hpet->base + offset) = value;
}

void hpet_init(hpet_t* hpet, acpi_sdt_header_t* acpi_hpet_table) {
    hpet_table_t* table = (hpet_table_t*)acpi_hpet_table;

    // retrieve the physical HPET base from the ACPI table
    paddr_t physical_base = (paddr_t)table->address;

    // map the HPET controller into the kernel's vaddr space
    vaddr_t virt_base = vmm_map_physical_page(&kernel_space, physical_base, PAGE_PRESENT | PAGE_WRITABLE | PAGE_CACHE_DISABLE);
    hpet->base = virt_base;

    // enable the HPET by setting the enable bit in the General Configuration register
    uint64_t gen_config = hpet_read_hpet_register(hpet, HPET_GENERAL_CONFIGURATION_OFFSET);
    gen_config |= HPET_ENABLE_BIT;
    hpet_write_hpet_register(hpet, HPET_GENERAL_CONFIGURATION_OFFSET, gen_config);
}

u64 hpet_read_counter(hpet_t* hpet) {
    return hpet_read_hpet_register(hpet, HPET_MAIN_COUNTER_OFFSET);
}

u64 hpet_query_frequency(hpet_t* hpet) {
    uint64_t gc_id_reg = hpet_read_hpet_register(hpet, HPET_GENERAL_CAPABILITIES_ID_REGISTER);
    uint32_t clock_period_fs = (uint32_t)(gc_id_reg >> 32);  // the upper 32 bits contain the period

    if (clock_period_fs == 0) {
        return 0;
    }

    // convert the period from femtoseconds to Hz
    return 1000000000000000ULL / clock_period_fs;
}