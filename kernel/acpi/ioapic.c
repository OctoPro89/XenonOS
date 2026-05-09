#include <acpi/ioapic.h>
#include <memory/vmm.h>

__PRIVILEGED_DATA static ioapic_t primary_ioapic;

__PRIVILEGED_CODE static u32 ioapic_read(ioapic_t* ioapic, u8 reg_off) {
    u32 result = 0;

    *(volatile u32*)(ioapic->virtual_base + IOAPIC_REGSEL) = reg_off;
    result = *(volatile u32*)(ioapic->virtual_base + IOAPIC_IOWIN);

    return result;
}

__PRIVILEGED_CODE static void ioapic_write(ioapic_t* ioapic, u8 reg_off, u32 data) {
    *(volatile u32*)(ioapic->virtual_base + IOAPIC_REGSEL) = reg_off;
    *(volatile u32*)(ioapic->virtual_base + IOAPIC_IOWIN) = data;
}

__PRIVILEGED_CODE static ioapic_t ioapic_init(u64 physbase, u64 gsib) {
    ioapic_t ioapic;
    ioapic.physical_base = physbase;
    ioapic.global_intr_base = gsib;
    ioapic.virtual_base = vmm_map_physically_contiguous_pages(&kernel_space, physbase, 2, PAGE_PRESENT | PAGE_WRITABLE | PAGE_CACHE_DISABLE);

    // initialize APIC ID and version
    ioapic.apic_id = (ioapic_read(&ioapic, IOAPICID) >> 24) & 0xF0;
    ioapic.apic_version = ioapic_read(&ioapic, IOAPICVER);

    ioapic.redirection_entry_count = (ioapic_read(&ioapic, IOAPICVER) >> 16) + 1;
    ioapic.global_intr_base = gsib;
}

__PRIVILEGED_CODE void ioapic_create(u64 physbase, u64 gsib) {
    primary_ioapic = ioapic_init(physbase, gsib);
}