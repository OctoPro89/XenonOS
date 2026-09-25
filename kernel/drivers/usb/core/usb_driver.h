#pragma once

#include <drivers/usb/core/usb_driver.h>
#include <xlibc/xinterface.h>
#include <xlibc/xstdint.h>

#define USB_CORE_MATCH_ANY ((u8)0xFF)

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

#define IUSBDRIVER_METHODS(x) \
    x(void, finalize_create, (void* self)) \
    x(void, destroy, (void* self)) \
    x(b8, probe, (void* self, usb_device_t* dev, usb_interface_t* iface)) \
    x(void, run, (void* self)) \
    x(void, disconnect, (void *self))

INTERFACE(IUSBDRIVER, IUSBDRIVER_MEMBERS, IUSBDRIVER_METHODS);

typedef IUSBDRIVER* (*usb_core_interface_driver_factory_func)(usb_device_t* dev, usb_interface_t* interface);

typedef struct {
    usb_core_interface_match_t match;
    usb_core_interface_driver_factory_func create;
    const char* name;
} usb_core_interface_driver_entry_t;

