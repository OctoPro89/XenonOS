#include "pci.h"

#include <arch/x86_64/io.h>
#include <xlibc/xstddef.h>
#include <xlibc/string.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static PCI_Bus pci_bus;

u32 pci_config_read(u8 bus, u8 device, u8 function, u8 offset) {
    u32 address = (1 << 31)               // enable bit
                     | ((bus & 0xFF) << 16)
                     | ((device & 0x1F) << 11)
                     | ((function & 0x07) << 8)
                     | (offset & 0xFC);      // align to 4 bytes
    x64_outl(PCI_CONFIG_ADDRESS, address);
    return x64_inl(PCI_CONFIG_DATA);
}

void pci_config_write(u8 bus, u8 device, u8 function, u8 offset, u32 value) {
    u32 address = (1 << 31)
                     | ((bus & 0xFF) << 16)
                     | ((device & 0x1F) << 11)
                     | ((function & 0x07) << 8)
                     | (offset & 0xFC);
    x64_outl(PCI_CONFIG_ADDRESS, address);
    x64_outl(PCI_CONFIG_DATA, value);
}

void pci_add_device(u8 bus, u8 device, u8 function) {
    if (pci_bus.count >= PCI_MAX_DEVICES) return;

    u32 data0 = pci_config_read(bus, device, function, 0x00);
    u16 vendor = data0 & 0xFFFF;
    if (vendor == 0xFFFF) return;  // no device

    u16 device_id = (data0 >> 16) & 0xFFFF;
    u32 data1 = pci_config_read(bus, device, function, 0x08);
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
        dev->bar[i] = pci_config_read(bus, device, function, 0x10 + i * 4);
    }
}

void pci_scan() {
    memset(&pci_bus, 0, sizeof(pci_bus));

    for (u16 bus = 0; bus < 256; bus++) {
        for (u8 device = 0; device < 32; device++) {
            u32 header = pci_config_read(bus, device, 0, 0x00);
            if ((header & 0xFFFF) == 0xFFFF) { continue; }

            pci_add_device((u8)bus, device, 0);

            // check for multi-function
            if (pci_config_read((u8)bus, device, 0, 0x0C) & (1 << 7)) {
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

void pci_enable_bus_master(PCI_Device* dev) {
    uint32_t cmd = pci_config_read(dev->bus, dev->device, dev->function, 0x04);
    cmd |= (1 << 2); // Bus Master Enable
    cmd |= (1 << 1); // Memory Space Enable
    pci_config_write(dev->bus, dev->device, dev->function, 0x04, cmd);
}