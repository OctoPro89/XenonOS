#pragma once

#include <drivers/usb/core/usb_driver.h>
#include <drivers/usb/core/usb_device.h>
#include <xlibc/xinterface.h>
#include <xlibc/xstdint.h>

#define USB_MATCH_ANY ((u8)0xFF)

#define USB_MAKE_MATCH(cls, sub, proto) (usb_core_interface_match_t){ .interface_class = cls, .interface_subclass = sub, .interface_protocol = proto }

typedef struct {
    u8 interface_class; // USB_MATCH_ANY_8 = wildcard
    u8 interface_subclass; // USB_MATCH_ANY_8 = wildcard
    u8 interface_protocol; // USB_MATCH_ANY_8 = wildcard
} usb_core_interface_match_t;

typedef struct usb_device usb_device_t;

#define IUSBDRIVER_MEMBERS(x) \
    x(const char*, name) \
    x(usb_device_t*, bound_device) \
    x(u8, bound_slot_id) \
    x(u8, bound_interface_index)

#define IUSBDRIVER_METHODS(x, self) \
    x(void, finalize_create, (self)) \
    x(void, destroy, (self)) \
    x(b8, probe, (self, usb_device_t* dev, usb_interface_t* iface)) \
    x(void, run, (self)) \
    x(void, disconnect, (self))

INTERFACE(IUSBDRIVER, IUSBDRIVER_MEMBERS, IUSBDRIVER_METHODS);

typedef IUSBDRIVER* (*usb_core_interface_driver_factory_func)(usb_device_t* dev, usb_interface_t* interface);

typedef struct {
    usb_core_interface_match_t match;
    usb_core_interface_driver_factory_func create;
    const char* name;
} usb_core_interface_driver_entry_t;

