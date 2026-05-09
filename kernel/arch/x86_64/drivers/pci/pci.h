/*
    PCI (Peripheral Component Interconnect) driver for XenonOS
*/

#pragma once
#include <xlibc/xstdint.h>
#include <memory/memory_types.h>

#define PCI_MAX_DEVICES 256

typedef struct {
    paddr_t base;
    u32 size;
    b8 is_io;
    b8 valid;
} pci_bar_t;

typedef struct {
    u8 bus;
    u8 device;
    u8 function;
    u16 vendor_id;
    u16 device_id;
    u8 class_code;
    u8 subclass;
    u8 prog_if;
    pci_bar_t bar[6]; // BAR0-BAR5
} PCI_Device;

typedef struct {
    PCI_Device devices[PCI_MAX_DEVICES];
    u32 count;
} PCI_Bus;

u32 pci_config_read(u8 bus, u8 device, u8 function, u8 offset);
void pci_config_write(u8 bus, u8 device, u8 function, u8 offset, u32 value);
void pci_add_device(u8 bus, u8 device, u8 function);
void pci_scan();
PCI_Device* pci_find_ahci();
PCI_Device* pci_find_xhci();
void pci_enable_bus_master(PCI_Device* dev);