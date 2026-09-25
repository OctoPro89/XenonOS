#pragma once
#include <drivers/usb/core/usb_transfer.h>
#include <drivers/usb/core/usb_driver.h>
#include <drivers/usb/core/usb_device.h>
#include <drivers/usb/hid/hid_parser.h>

typedef enum {
    HID_BINDING_KIND_KEYBOARD = 0,
    HID_BINDING_KIND_MOUSE = 1
} hid_binding_kind_t;

typedef struct {
    u8 report_id;
    hid_binding_kind_t binding_kind;
} hid_handler_binding_t;

#define IHIDDRIVER_MEMBERS(x) \
    IUSBDRIVER_MEMBERS(x) \
    x(usb_interface_t*, iface) \
    x(usb_hid_report_layout_t, layout) \
    x(hid_handler_binding_t*, bindings) \
    x(u16, binding_count) \
    x(b8, disconnected) \
    x(u32, payload_length) \
    x(usb_interrupt_in_stream_t, stream) \

#define IHIDDRIVER_METHODS(x) \
    IUSBDRIVER_METHODS(x)

INTERFACE(IHIDDRIVER, IHIDDRIVER_MEMBERS, IHIDDRIVER_METHODS);