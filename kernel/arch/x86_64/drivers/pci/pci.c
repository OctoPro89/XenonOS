#include "pci.h"

#include <arch/x86_64/io.h>
#include <xlibc/xstddef.h>
#include <xlibc/string.h>
#include <memory/vmm.h>
#include <memory/paging.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define MSI_CONTROL      0x2
#define MSI_ADDR_LOW     0x4
#define MSI_ADDR_HIGH    0x8
#define MSI_DATA_32      0x8

#define PCI_CAP_ID_MSIX 0x11

#define MSIX_CONTROL        0x02
#define MSIX_TABLE          0x04
#define MSIX_PBA            0x08

#define MSIX_ENABLE         (1 << 15)
#define MSIX_FUNCTION_MASK  (1 << 14)

static PCI_Bus pci_bus;

u8 pci_config_read8(u8 bus, u8 dev, u8 fn, u8 off) {
    u32 v = pci_config_read32(bus, dev, fn, off & ~3);
    return (v >> ((off & 3) * 8)) & 0xFF;
}

u16 pci_config_read16(u8 bus, u8 dev, u8 fn, u8 off) {
    u32 v = pci_config_read32(bus, dev, fn, off & ~3);
    return (v >> ((off & 2) * 8)) & 0xFFFF;
}

u32 pci_config_read32(u8 bus, u8 device, u8 function, u8 offset) {
    u32 address = (1 << 31)               // enable bit
                     | ((bus & 0xFF) << 16)
                     | ((device & 0x1F) << 11)
                     | ((function & 0x07) << 8)
                     | (offset & 0xFC);      // align to 4 bytes
    x64_outl(PCI_CONFIG_ADDRESS, address);
    return x64_inl(PCI_CONFIG_DATA);
}

void pci_config_write16(u8 bus, u8 dev, u8 fn, u8 off, u16 value) {
    u32 aligned = off & ~3;
    u32 shift = (off & 2) * 8;

    u32 old = pci_config_read32(bus, dev, fn, aligned);

    old &= ~(0xFFFF << shift);
    old |= ((u32)value << shift);

    pci_config_write32(bus, dev, fn, aligned, old);
}

void pci_config_write32(u8 bus, u8 device, u8 function, u8 offset, u32 value) {
    u32 address = (1 << 31)
                     | ((bus & 0xFF) << 16)
                     | ((device & 0x1F) << 11)
                     | ((function & 0x07) << 8)
                     | (offset & 0xFC);
    x64_outl(PCI_CONFIG_ADDRESS, address);
    x64_outl(PCI_CONFIG_DATA, value);
}

static u8 pci_find_capability(PCI_Device* dev, u8 cap_id) {
    u32 reg = pci_config_read32(dev->bus, dev->device, dev->function, 0x04);
    u16 status = (reg >> 16) & 0xFFFF;

    if (!(status & PCI_STATUS_CAP_LIST)) {
        return 0;
    }

    u8 ptr = pci_config_read32(dev->bus, dev->device, dev->function, PCI_CAP_PTR) & 0xFF;

    while (ptr) {
        u32 cap = pci_config_read32(dev->bus, dev->device, dev->function, ptr);

        u8 id = cap & 0xFF;
        u8 next = (cap >> 8) & 0xFF;

        if (id == cap_id) {
            return ptr;
        }

        ptr = next;
    }

    return 0;
}

static void pci_probe_bar(PCI_Device* dev, u8 bar_index) {
    u8 offset = 0x10 + bar_index * 4;

    u32 original = pci_config_read32(dev->bus, dev->device, dev->function, offset);

    if (original == 0 || original == 0xFFFFFFFF) {
        dev->bar[bar_index].valid = 0;
        return;
    }

    pci_config_write32(dev->bus, dev->device, dev->function, offset, 0xFFFFFFFF);
    u32 mask = pci_config_read32(dev->bus, dev->device, dev->function, offset);
    pci_config_write32(dev->bus, dev->device, dev->function, offset, original);

    dev->bar[bar_index].is_io = original & 1;
    dev->bar[bar_index].valid = 1;

    if (original & 1) {
        // I/O BAR
        mask &= ~0x3;
        dev->bar[bar_index].base = original & ~0x3;
        dev->bar[bar_index].size = (~mask) + 1;
    } else {
        // Memory BAR
        mask &= ~0xF;
        dev->bar[bar_index].base = original & ~0xF;
        dev->bar[bar_index].size = (~mask) + 1;

        if (((original >> 1) & 0x3) == 0x2) {
            // 64-bit BAR uses next register
            u32 high = pci_config_read32(dev->bus, dev->device, dev->function, offset + 4);

            dev->bar[bar_index].base |= ((u64)high << 32);

            // skip next BAR
            dev->bar[bar_index + 1].valid = 0;
        }
    }
}

void pci_enable_msi(PCI_Device* dev, u8 vector) {
    u8 cap = pci_find_capability(dev, 0x05); // MSI

    if (!cap) return;

    // Read control
    u32 ctrl = pci_config_read32(dev->bus, dev->device, dev->function, cap + MSI_CONTROL);

    // Enable MSI (bit 0)
    ctrl |= 1;

    pci_config_write32(dev->bus, dev->device, dev->function, cap + MSI_CONTROL, ctrl);

    // APIC base address (fixed for x86)
    u32 apic_addr = 0xFEE00000;

    pci_config_write32(dev->bus, dev->device, dev->function, cap + MSI_ADDR_LOW, apic_addr);

    // Vector goes into low byte
    pci_config_write32(dev->bus, dev->device, dev->function, cap + MSI_DATA_32, vector);
}

b8 pci_enable_msix(PCI_Device* dev, u8 vector) {
    u8 cap = pci_find_capability(dev, PCI_CAP_ID_MSIX);

    if (!cap) {
        return false;
    }

    u16 control = pci_config_read16(dev->bus, dev->device, dev->function, cap + MSIX_CONTROL);

    u32 table_info = pci_config_read32(dev->bus, dev->device, dev->function, cap + MSIX_TABLE);

    u8 bir = table_info & 0x7;
    u32 table_offset = table_info & ~0x7;

    if (bir >= 6) {
        return false;
    }

    vaddr_t bar_virt = pci_map_bar(dev, bir);

    if (!bar_virt) {
        return false;
    }

    volatile pci_msix_table_entry_t* table = (volatile pci_msix_table_entry_t*)(bar_virt + table_offset);

    table[0].vector_control = 1; // mask vector while programming
    table[0].msg_addr_low  = 0xFEE00000; // MSI address (local APIC)
    table[0].msg_addr_high = 0;
    table[0].msg_data = vector;
    table[0].vector_control = 0; // unmask vector

    control |= MSIX_ENABLE; // enable MSI-X
    control &= ~MSIX_FUNCTION_MASK; // clear function mask

    pci_config_write16(dev->bus, dev->device, dev->function, cap + MSIX_CONTROL, control);

    // disable legacy INTx
    u16 cmd = pci_config_read16(dev->bus, dev->device, dev->function, 0x04);
    cmd |= (1 << 10);
    pci_config_write16(dev->bus, dev->device, dev->function, 0x04, cmd);

    return true;
}

void pci_add_device(u8 bus, u8 device, u8 function) {
    if (pci_bus.count >= PCI_MAX_DEVICES) return;

    u32 data0 = pci_config_read32(bus, device, function, 0x00);
    u16 vendor = data0 & 0xFFFF;
    if (vendor == 0xFFFF) return;  // no device

    u16 device_id = (data0 >> 16) & 0xFFFF;
    u32 data1 = pci_config_read32(bus, device, function, 0x08);
    u8 class_code = (data1 >> 24) & 0xFF;
    u8 subclass   = (data1 >> 16) & 0xFF;
    u8 prog_if    = (data1 >> 8) & 0xFF;

    PCI_Device* dev = &pci_bus.devices[pci_bus.count++];
    dev->bus = bus;
    dev->device = device;
    dev->function = function;
    dev->vendor_id = vendor;
    dev->device_id = device_id;
    dev->class_code = class_code;
    dev->subclass = subclass;
    dev->prog_if = prog_if;

    // Read BAR0-BAR5
    for (int i = 0; i < 6; i++) {
        // dev->bar[i] = pci_config_read32(bus, device, function, 0x10 + i * 4);
        pci_probe_bar(dev, i);
    }
}

void pci_scan() {
    memset(&pci_bus, 0, sizeof(pci_bus));

    for (u16 bus = 0; bus < 256; bus++) {
        for (u8 device = 0; device < 32; device++) {
            u32 header = pci_config_read32(bus, device, 0, 0x00);
            if ((header & 0xFFFF) == 0xFFFF) { continue; }

            pci_add_device((u8)bus, device, 0);

            // check for multi-function
            if (pci_config_read32((u8)bus, device, 0, 0x0C) & (1 << 7)) {
                for (u8 func = 1; func < 8; func++)
                    pci_add_device((u8)bus, device, func);
            }
        }
    }
}

PCI_Device* pci_find_ahci() {
    for (uint32_t i = 0; i < pci_bus.count; i++) {
        PCI_Device *dev = &pci_bus.devices[i];
        if (dev->class_code == 0x01 && dev->subclass == 0x06 && dev->prog_if == 0x01) {
            return dev;
        }
    }
    return NULL;
}

PCI_Device* pci_find_xhci() {
    for (u32 i = 0; i < pci_bus.count; i++) {
        PCI_Device* dev = &pci_bus.devices[i];

        if (dev->class_code == 0x0C && dev->subclass == 0x03 && dev->prog_if == 0x30) {
            return dev;
        }
    }
    return NULL;
}

void pci_enable_bus_master(PCI_Device* dev) {
    uint32_t cmd = pci_config_read32(dev->bus, dev->device, dev->function, 0x04);
    cmd |= (1 << 2); // Bus Master Enable
    cmd |= (1 << 1); // Memory Space Enable
    pci_config_write32(dev->bus, dev->device, dev->function, 0x04, cmd);
}

vaddr_t pci_map_bar(PCI_Device* dev, u8 bar_index) {
    if (bar_index >= 6) {
        return 0;
    }

    pci_bar_t* bar = &dev->bar[bar_index];

    if (!bar->valid || bar->is_io) {
        return 0;
    }

    size_t pages = (bar->size + PAGE_SIZE - 1) / PAGE_SIZE;

    return vmm_map_physically_contiguous_pages(
        &kernel_space,
        bar->base,
        pages,
        PAGE_PRESENT | PAGE_WRITABLE | PAGE_CACHE_DISABLE
    );
}