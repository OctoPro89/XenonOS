/*
    PCI (Peripheral Component Interconnect) driver for XenonOS
*/

#pragma once
#include <kernel.h>
#include <xlibc/xstdint.h>
#include <memory/memory_types.h>

#define PCI_MAX_DEVICES 256
#define PCI_STATUS_CAP_LIST (1 << 4)
#define PCI_CAP_PTR 0x34

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

typedef struct {
    u8 id;
    u8 next;
    u16 data_off;
} pci_cap_t;

typedef struct __packed__ {
    u32 msg_addr_low;
    u32 msg_addr_high;
    u32 msg_data;
    u32 vector_control;
} pci_msix_table_entry_t;

u8 pci_config_read8(u8 bus, u8 device, u8 function, u8 offset);
u16 pci_config_read16(u8 bus, u8 device, u8 function, u8 offset);
u32 pci_config_read32(u8 bus, u8 device, u8 function, u8 offset);
void pci_config_write16(u8 bus, u8 device, u8 function, u8 offset, u16 value);
void pci_config_write32(u8 bus, u8 device, u8 function, u8 offset, u32 value);

void pci_enable_msi(PCI_Device* dev, u8 vector);
b8 pci_enable_msix(PCI_Device* dev, u8 vector);

void pci_add_device(u8 bus, u8 device, u8 function);
void pci_scan();
PCI_Device* pci_find_ahci();
PCI_Device* pci_find_xhci();
void pci_enable_bus_master(PCI_Device* dev);

vaddr_t pci_map_bar(PCI_Device* dev, u8 bar_index);