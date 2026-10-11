#pragma once

#include <drivers/usb/core/usb_transfer.h>
#include <drivers/usb/core/usb_driver.h>
#include <drivers/usb/core/usb_device.h>
#include <drivers/usb/mass_storage/msc_bot.h>

#define MSC_SUBCLASS_SCSI            0x06
#define MSC_PROTOCOL_BULK_ONLY       0x50

typedef struct msc_driver msc_driver_t;

typedef struct msc_driver {
    INTERFACE_IMPLEMENT(IUSBDRIVER_MEMBERS, IUSBDRIVER_METHODS);

    usb_device_t* dev;
    usb_interface_t* iface;

    // USB Mass Storage Bulk-Only transport state
    msc_bot_t bot;

    // Device capacity, populated after SCSI discovery
    u64 block_count;
    u32 block_size;

    b8 initialized;
    b8 disconnected;
} msc_driver_t;

IUSBDRIVER* msc_driver_factory(usb_device_t* dev, usb_interface_t* iface);