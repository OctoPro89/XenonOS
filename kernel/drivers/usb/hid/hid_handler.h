#pragma once
#include <drivers/usb/hid/hid_parser.h>
#include <xlibc/xinterface.h>

#define IHIDHANDLER_MEMBERS(x)
#define IHIDHANDLER_METHODS(x, self) \
    x(b8, init, (self, const usb_hid_report_layout_t* layout, const usb_hid_input_report_info_t* report)) \
    x(void, on_report, (self, const u8* data, u32 length)) \
    x(void, destroy, (self))

/**
 * Base interface for device type specific report processing.
 * The HID driver instantiates the appropriate handler after
 * parsing the report descriptor and detecting usage pages.
 * 
 * Methods:
 * init - Called once after the report descriptor is parsed, before the interupt
 * transfer loop begins. The handler is initialized against a single input report
 * within the interface layout.
 * 
 * on_report - Called each time a new HID report arrives from the device.
 * Data points to the raw interrupt IN buffer, length is the number of bytes received
 */
INTERFACE(IHIDHANDLER, IHIDHANDLER_MEMBERS, IHIDHANDLER_METHODS);