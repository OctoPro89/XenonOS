#pragma once
#include <drivers/usb/core/usb_transfer.h>
#include <drivers/usb/core/usb_driver.h>
#include <drivers/usb/core/usb_device.h>
#include <drivers/usb/hid/hid_parser.h>
#include <drivers/usb/hid/hid_handler.h>

typedef enum {
    HID_BINDING_KIND_KEYBOARD = 0,
    HID_BINDING_KIND_MOUSE = 1
} hid_binding_kind_t;

typedef struct {
    u8 report_id;
    hid_binding_kind_t binding_kind;
    IHIDHANDLER* handler;
} hid_handler_binding_t;

typedef struct hid_driver hid_driver_t;

typedef struct hid_driver {
    INTERFACE_IMPLEMENT(IUSBDRIVER_MEMBERS, IUSBDRIVER_METHODS);
    // const char* name; usb_device_t* bound_device; uint8_t bound_slot_id; uint8_t bound_interface_index; void (*finalize_create) (hid_driver_t*); void (*destroy) (hid_driver_t*); b8 (*probe) (hid_driver_t*, usb_device_t* dev, usb_interface_t* iface); void (*run) (hid_driver_t*); void (*disconnect) (hid_driver_t*);
    usb_interface_t* iface;
    usb_hid_report_layout_t layout;
    hid_handler_binding_t* bindings;
    u16 binding_count;
    b8 disconnected;
    u32 payload_length;
    usb_interrupt_in_stream_t* stream;
} hid_driver_t;

IUSBDRIVER* hid_driver_factory(usb_device_t* dev, usb_interface_t* iface);