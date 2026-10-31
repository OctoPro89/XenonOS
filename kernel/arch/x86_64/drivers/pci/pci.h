/**
 *  PCI (Peripheral Component Interconnect) driver for XenonOS
 */

// TODO: PCI driver interface

#pragma once

#include <kernel.h>
#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <memory/memory_types.h>

#define PCI_MAX_DEVICES 256
#define PCI_STATUS_CAP_LIST (1 << 4)
#define PCI_CAP_PTR 0x34

typedef struct {
    paddr_t base;
    u64 size;
    b8 is_io;
    b8 is_64bit;
    b8 prefetchable;
    b8 valid;
    vaddr_t mapped_base;
    size_t mapped_pages;
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

/**
 * @brief Read PCI config8
 * @note Atomic
 */
u8 pci_config_read8(u8 bus, u8 device, u8 function, u8 offset);

/**
 * @brief Read PCI config16
 * @note Atomic
 */
u16 pci_config_read16(u8 bus, u8 device, u8 function, u8 offset);

/**
 * @brief Read PCI config32
 * @note Atomic
 */
u32 pci_config_read32(u8 bus, u8 device, u8 function, u8 offset);

/**
 * @brief Write PCI config16
 * @note Atomic
 */
void pci_config_write16(u8 bus, u8 device, u8 function, u8 offset, u16 value);

/**
 * @brief Write PCI config32
 * @note Atomic
 */
void pci_config_write32(u8 bus, u8 device, u8 function, u8 offset, u32 value);

b8 pci_enable_msi(PCI_Device* dev, u8 vector);
b8 pci_enable_msix(PCI_Device* dev, u8 vector);

void pci_add_device(u8 bus, u8 device, u8 function);

/**
 * @brief Initializes the PCI subsystem and scans for devices
 */
void pci_scan();

PCI_Device* pci_find_ahci();
PCI_Device* pci_find_xhci();

void pci_enable_bus_master(PCI_Device* dev);

vaddr_t pci_map_bar(PCI_Device* dev, u8 bar_index);

/**
 * @brief Hold the PCI config lock
 */
void pci_config_lock_irqsave(u64* flags);

/**
 * @brief Unlock the PCI config lock
 */
void pci_config_unlock_irqrestore(u64 flags);

/**
 * @brief Read PCI config32
 * @note Caller must hold the PCI config lock
 */
u32 pci_config_read32_locked(u8 bus, u8 device, u8 function, u8 offset);

/**
 * @brief Write PCI config32
 * @note Caller must hold the PCI config lock
 */
void pci_config_write32_locked(u8 bus, u8 device, u8 function, u8 offset, u32 value);