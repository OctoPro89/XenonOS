#pragma once

#include <xlibc/xstdint.h>

typedef struct {
    u8 address; // bEndpointAddress (e.g. 0x81 = EP1 IN)
    u8 transfer_type; // 0 = control, 1 = isochronous, 2 = bulk, 3 = interrupt
    u16 max_packet_size;
    u8 interval; // polling interval (interrupt / isochronous)
} usb_endpoint_t;

#define USB_ENDPOINT_NUMBER(x) ((u8)((x).address & 0x0F))
#define USB_ENDPOINT_IS_IN(x) ((b8)(((x).address & 0x80) != 0))

typedef struct {
    u8 interface_number;
    u8 alternate_setting;
    u8 interface_class;
    u8 interface_subclass;
    u8 interface_protocol;
    u16 hid_report_desc_length;
    u8 num_endpoints;
    usb_endpoint_t endpoints[16];
} usb_interface_t;

typedef struct usb_device {
    // identity (from device descriptor)
    u16 vid;
    u16 pid;
    u16 bcd_usb;
    u8 device_class;
    u8 device_subclass;
    u8 device_protocol;
    u8 speed;

    // Active configuration
    u8 config_value;
    u8 num_interfaces;
    usb_interface_t interfaces[16];

    // Hub topology
    u8 is_hub;
    u8 hub_num_ports;

    // opaque handles for the USB Core transfer API
    void* hcd; // host controller driver instance (xhci_driver_t*)
    void* hcd_device; // driver device handle (xhci_device_t*)

    u8 active_driver_count;
    b8 disconnect_pending;
    b8 hcd_teardown_complete;
    b8 finalize_started; // final release path has claimed ownership
} usb_device_t;