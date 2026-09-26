#pragma once
#include <xlibc/xstdint.h>
#include <arch/x86_64/drivers/pci/pci.h>
#include <drivers/usb/core/usb_transfer.h>
#include <drivers/usb/xhci_ext_cap.h>
#include <drivers/usb/xhci_device.h>
#include <drivers/usb/xhci_rings.h>
#include <drivers/usb/xhci_regs.h>

typedef struct {
    const char* name;
    PCI_Device* pci_device;
    vaddr_t xhc_base;
    volatile xhci_capability_registers_t* cap_regs; // NOTE: MUST be defined as volatile because controller will change values behind scenes
    volatile xhci_operational_registers_t* op_regs;
    volatile xhci_runtime_registers_t* runtime_regs;

    // linked list of extended capabilities
    xhci_extended_capability_t extended_capabilities_head;

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
    b8 sixty_four_byte_context_size; // CSZ
    b8 port_power_control;
    b8 port_indicators;
    b8 light_reset_capability;
    u32 extended_capabilities_offset;

    /* paddr_t */ u64* dcbaa;
    /* vaddr_t */ u64* dcbaa_virtual_addresses;

    // main command ring
    xhci_command_ring_t command_ring;

    // main event ring
    xhci_event_ring_t event_ring;

    // doorbell register array manager
    xhci_doorbell_manager_t doorbell_manager;

    xhci_command_completion_trb_t* command_completion_events[XHCI_RINGS_MAX_DEQUEUEABLE_EVENTS];
    u64 command_completion_event_count;

    // flag indicating if we have a command completion event
    volatile u8 command_irq_completed;

    u8 irq_vector;

    // USB3.x specific ports (zero-based)
    u8 usb3_ports[255];
    u8 usb3_port_count;

    xhci_device_t** port_devices;
    xhci_device_t** slot_devices;

    // deferred endpoint doorbells, run after event processing finishes
    struct pending_doorbell { u8 slot_id; u8 target; } pending_doorbells[32];
    u8 pending_doorbell_count;
} xhci_driver_t;

b8 xhci_driver_init_driver(xhci_driver_t* driver);
b8 xhci_driver_start_device(xhci_driver_t* driver);
b8 xhci_driver_shutdown_device(xhci_driver_t* driver);

void xhci_driver_parse_capability_registers(xhci_driver_t* driver);
void xhci_driver_parse_extended_capabilities(xhci_driver_t* driver);
void xhci_driver_log_capability_registers(xhci_driver_t* driver);

b8 xhci_driver_reset_host_controller(xhci_driver_t* driver);
b8 xhci_driver_reset_port(xhci_driver_t* driver, u8 port_num);

// Public transfer API for USB Core
b8 xhci_driver_usb_control_transfer(xhci_driver_t* driver, xhci_device_t* device, u8 request_type, u8 request, u16 value, u16 index, void* data, u16 length);
b8 xhci_driver_usb_submit_transfer(xhci_driver_t* driver, xhci_device_t* device, u8 endpoint_addr, void* buffer, u32 length);
void xhci_driver_usb_cancel_transfer(xhci_driver_t* driver, xhci_device_t* device, usb_transfer_request_t* request);
b8 xhci_driver_usb_open_interrupt_in_stream(xhci_driver_t* driver, xhci_device_t* device, u8 endpoint_addr, u32 payload_length);
b8 xhci_driver_usb_read_interrupt_in_stream(xhci_driver_t* driver, xhci_device_t* device, u8 endpoint_addr, void* buffer, u32 buffer_len, u32* out_length);
b8 xhci_driver_usb_close_interrupt_in_stream(xhci_driver_t* driver, xhci_device_t* device, u8 endpoint_addr);

void xhci_driver_release_disconnected_device(xhci_driver_t* driver, xhci_device_t* device);
/**
 * @note This function assumes host controller has already been started successfully
 */
void xhci_driver_run(xhci_driver_t* driver);