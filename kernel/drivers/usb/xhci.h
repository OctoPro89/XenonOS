#pragma once
#include <xlibc/xstdint.h>
#include <arch/x86_64/drivers/pci/pci.h>
#include <drivers/usb/xhci_regs.h>

typedef struct {
    const char* name;
    PCI_Device* pci_device;
    vaddr_t xhc_base;
    volatile xhci_capability_registers* cap_regs; // NOTE: MUST be defined as volatile because controller will change values behind scenes
    volatile xhci_operational_registers* op_regs;

    // CAPLENGTH
    u8 capability_regs_length;

    // HCSPARAMS1
    u8 max_device_slots;
    u8 max_interrupters;
    u8 max_ports;

    // HCSPARAMS2
    u8 isochronous_scheduling_threshold;
    u8 erst_max;
    u8 max_scratchpad_buffers;

    // HCCPARAMS1
    b8 sixty_four_bit_addressing_capability;
    b8 bandwidth_negotiation_capability;
    b8 sixty_four_byte_context_size;
    b8 port_power_control;
    b8 port_indicators;
    b8 light_reset_capability;
    u32 extended_capabilities_offset;
} xhci_driver_t;

b8 xhci_driver_init_device(xhci_driver_t* driver);
b8 xhci_driver_start_device(xhci_driver_t* driver);
b8 xhci_driver_shutdown_device(xhci_driver_t* driver);

void xhci_driver_parse_capability_registers(xhci_driver_t* driver);
void xhci_driver_log_capability_registers(xhci_driver_t* driver);

b8 xhci_driver_reset_host_controller(xhci_driver_t* driver);