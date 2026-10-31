#include "pci.h"

#include <arch/x86_64/sync/sync.h>
#include <arch/x86_64/io.h>
#include <xlibc/xstddef.h>
#include <xlibc/string.h>
#include <memory/vmm.h>
#include <memory/paging.h>
#include <limits.h>

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
static spinlock_t pci_cfg_lock;

static u8 pci_config_read8_unlocked(u8 bus, u8 dev, u8 fn, u8 off);
static u16 pci_config_read16_unlocked(u8 bus, u8 dev, u8 fn, u8 off);
static u32 pci_config_read32_unlocked(u8 bus, u8 device, u8 function, u8 offset);
static void pci_config_write16_unlocked(u8 bus, u8 dev, u8 fn, u8 off, u16 value);
static void pci_config_write32_unlocked(u8 bus, u8 device, u8 function, u8 offset, u32 value);

static u8 pci_config_read8_unlocked(u8 bus, u8 dev, u8 fn, u8 off) {
    u32 v = pci_config_read32_unlocked(bus, dev, fn, off & ~3);
    return (v >> ((off & 3) * 8)) & 0xFF;
}

static u16 pci_config_read16_unlocked(u8 bus, u8 dev, u8 fn, u8 off) {
    u32 v = pci_config_read32_unlocked(bus, dev, fn, off & ~3);
    return (v >> ((off & 2) * 8)) & 0xFFFF;
}

static u32 pci_config_read32_unlocked(u8 bus, u8 device, u8 function, u8 offset) {
    u32 address = (1 << 31)               // enable bit
                     | ((bus & 0xFF) << 16)
                     | ((device & 0x1F) << 11)
                     | ((function & 0x07) << 8)
                     | (offset & 0xFC);      // align to 4 bytes
    x64_outl(PCI_CONFIG_ADDRESS, address);
    return x64_inl(PCI_CONFIG_DATA);
}

static void pci_config_write16_unlocked(u8 bus, u8 dev, u8 fn, u8 off, u16 value) {
    u32 aligned = off & ~3;
    u32 shift = (off & 2) * 8;

    u32 old = pci_config_read32_unlocked(bus, dev, fn, aligned);

    old &= ~(0xFFFF << shift);
    old |= ((u32)value << shift);

    pci_config_write32_unlocked(bus, dev, fn, aligned, old);
}

static void pci_config_write32_unlocked(u8 bus, u8 device, u8 function, u8 offset, u32 value) {
    u32 address = (1 << 31)
                     | ((bus & 0xFF) << 16)
                     | ((device & 0x1F) << 11)
                     | ((function & 0x07) << 8)
                     | (offset & 0xFC);
    x64_outl(PCI_CONFIG_ADDRESS, address);
    x64_outl(PCI_CONFIG_DATA, value);
}

u8 pci_config_read8(u8 bus, u8 device, u8 function, u8 offset) {
    u64 flags;
    spin_lock_irqsave(&pci_cfg_lock, &flags);

    u8 value = pci_config_read8_unlocked(bus, device, function, offset);

    spin_unlock_irqrestore(&pci_cfg_lock, flags);
    return value;
}

u16 pci_config_read16(u8 bus, u8 device, u8 function, u8 offset) {
    u64 flags;
    spin_lock_irqsave(&pci_cfg_lock, &flags);

    u16 value = pci_config_read16_unlocked(bus, device, function, offset);

    spin_unlock_irqrestore(&pci_cfg_lock, flags);
    return value;
}

u32 pci_config_read32(u8 bus, u8 device, u8 function, u8 offset) {
    u64 flags;
    spin_lock_irqsave(&pci_cfg_lock, &flags);

    u32 value = pci_config_read32_unlocked(bus, device, function, offset);

    spin_unlock_irqrestore(&pci_cfg_lock, flags);
    return value;
}

void pci_config_write16(u8 bus, u8 device, u8 function, u8 offset, u16 value) {
    u64 flags;
    spin_lock_irqsave(&pci_cfg_lock, &flags);

    pci_config_write16_unlocked(bus, device, function, offset, value);

    spin_unlock_irqrestore(&pci_cfg_lock, flags);
}

void pci_config_write32(u8 bus, u8 device, u8 function, u8 offset, u32 value) {
    u64 flags;
    spin_lock_irqsave(&pci_cfg_lock, &flags);

    pci_config_write32_unlocked(bus, device, function, offset, value);

    spin_unlock_irqrestore(&pci_cfg_lock, flags);
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

/**
 * @brief Probes a PCI BAR
 * @note Should be run during device initialization, before enabling the device or handing off to a driver. Also, a 64-bit BAR consumes the following BAR slot; the caller must skip that.
 * @attention This assumes conventional type-0 endpoint headers. TODO: PCI bridges have different BAR layout, should check header type before probing.
 */
static void pci_probe_bar(PCI_Device* dev, u8 bar_index) {
    if (!dev || bar_index >= 6) {
        return;
    }

    pci_bar_t* bar = &dev->bar[bar_index];
    memset(bar, 0, sizeof(*bar));

    u8 offset = (u8)(0x10 + bar_index * 4);

    u16 command = pci_config_read16(dev->bus, dev->device, dev->function, 0x04);

    // disable I/O and memory decoding during probing, do not probe a device while a driver is actively using it
    pci_config_write16(dev->bus, dev->device, dev->function, 0x04, command & ~((1u << 0) | (1u << 1)));

    u32 original_low = pci_config_read32(dev->bus, dev->device, dev->function, offset);

    b8 is_io = (original_low & 1u) != 0;
    b8 is_64bit = 0;
    u32 original_high = 0;

    if (!is_io) {
        u32 type = (original_low >> 1) & 3u;

        if (type == 2u && bar_index < 5) {
            is_64bit = 1;
            original_high = pci_config_read32(dev->bus, dev->device, dev->function, offset + 4);
        }
    }

    // probe both halves of a 64-bit BAR before reading either mask
    pci_config_write32(dev->bus, dev->device, dev->function, offset, 0xFFFFFFFFu);

    if (is_64bit) {
        pci_config_write32(dev->bus, dev->device, dev->function, offset + 4, 0xFFFFFFFFu);
    }

    u32 mask_low = pci_config_read32(dev->bus, dev->device, dev->function, offset);

    u32 mask_high = 0;

    if (is_64bit) {
        mask_high = pci_config_read32(dev->bus, dev->device, dev->function, offset + 4);
    }

    // restore original BAR values before restoring decoding 
    pci_config_write32(dev->bus, dev->device, dev->function, offset, original_low);

    if (is_64bit) {
        pci_config_write32(dev->bus, dev->device, dev->function, offset + 4, original_high);
    }

    pci_config_write16(dev->bus, dev->device, dev->function, 0x04, command);

    bar->is_io = is_io;
    bar->is_64bit = is_64bit;

    if (is_io) {
        u32 mask = mask_low & ~3u;
        u32 base = original_low & ~3u;

        if (mask == 0) {
            return;
        }

        bar->base = base;
        bar->size = (u32)(~mask + 1u);
        bar->valid = bar->size != 0;
        return;
    }

    // reserved memory BAR types are not supported here
    u32 type = (original_low >> 1) & 3u;
    if (type == 3u || (type == 2u && !is_64bit)) {
        return;
    }

    bar->prefetchable = (original_low & 8u) != 0;

    if (is_64bit) {
        u64 mask = ((u64)mask_high << 32)
                 | ((u64)mask_low & ~0xFULL);

        u64 base = ((u64)original_high << 32)
                 | ((u64)original_low & ~0xFULL);

        if (mask == 0) {
            return;
        }

        bar->base = base;
        bar->size = ~mask + 1ULL;
    } else {
        u32 mask = mask_low & ~0xFu;
        u32 base = original_low & ~0xFu;

        if (mask == 0) {
            return;
        }

        bar->base = base;
        bar->size = (u32)(~mask + 1u);
    }

    bar->valid = bar->size != 0;

    // A 64-bit BAR consumes the following BAR slot the caller must skip that slot
}

b8 pci_enable_msi(PCI_Device* dev, u8 vector) {
    if (!dev) {
        return false;
    }
    
    u8 cap = pci_find_capability(dev, 0x05); // MSI

    if (!cap) {
        return false;
    }

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

    return true;
}

b8 pci_enable_msix(PCI_Device* dev, u8 vector) {
    if (!dev) {
        return false;
    }
    
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
    for (u8 i = 0; i < 6; i++) {
        pci_probe_bar(dev, i);

        if (dev->bar[i].is_64bit) {
            i++;  // skip upper half of a 64-bit BAR
        }
    }
}

void pci_scan() {
    memset(&pci_bus, 0, sizeof(pci_bus));
    spin_lock_init(&pci_cfg_lock);

    for (u16 bus = 0; bus < 256; bus++) {
        for (u8 device = 0; device < 32; device++) {
            u32 header = pci_config_read32(bus, device, 0, 0x00);
            if ((header & 0xFFFF) == 0xFFFF) { continue; }

            pci_add_device((u8)bus, device, 0);

            // check for multi-function
            if (pci_config_read32((u8)bus, device, 0, 0x0C) & (1 << 7)) {
                for (u8 func = 1; func < 8; func++) {
                    pci_add_device((u8)bus, device, func);
                }
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
    if (!dev || bar_index >= 6) {
        return 0;
    }

    pci_bar_t* bar = &dev->bar[bar_index];

    if (!bar->valid || bar->is_io) {
        return 0;
    }

    // don't remap
    if (bar->mapped_base != 0) {
        return bar->mapped_base + (bar->base & (PAGE_SIZE - 1));
    }

    u64 offset = bar->base & (PAGE_SIZE - 1);
    paddr_t phys_page = bar->base & ~(paddr_t)(PAGE_SIZE - 1);

    if (bar->size > UINT64_MAX - offset) {
        return 0;
    }

    u64 span = bar->size + offset;

    if (span > UINT64_MAX - (PAGE_SIZE - 1)) {
        return 0;
    }

    u64 pages64 = (span + PAGE_SIZE - 1) / PAGE_SIZE;

    if (pages64 == 0 || pages64 > SIZE_MAX) {
        return 0;
    }

    size_t pages = (size_t)pages64;

    vaddr_t virt = vmm_map_physically_contiguous_pages(&kernel_space, phys_page, pages, PAGE_PRESENT | PAGE_WRITABLE | PAGE_CACHE_DISABLE);

    if (!virt) {
        return 0;
    }

    bar->mapped_base = virt;
    bar->mapped_pages = pages;

    return virt + offset;
}

void pci_config_lock_irqsave(u64* flags) {
    spin_lock_irqsave(&pci_cfg_lock, flags);
}

void pci_config_unlock_irqrestore(u64 flags) {
    spin_unlock_irqrestore(&pci_cfg_lock, flags);
}

u32 pci_config_read32_locked(u8 bus, u8 device, u8 function, u8 offset) {
    return pci_config_read32_unlocked(bus, device, function, offset);
}

void pci_config_write32_locked(u8 bus, u8 device, u8 function, u8 offset, u32 value) {
    pci_config_write32_unlocked(bus, device, function, offset, value);
}