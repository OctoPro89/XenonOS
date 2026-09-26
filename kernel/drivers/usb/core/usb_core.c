#include <drivers/usb/core/usb_core.h>
#include <drivers/usb/core/usb_device.h>
#include <xlibc/stdlib.h>
#include <xlibc/string.h>
#include <xlibc/stdio.h>

// tracks which usb_device_t is associated with each xhci_device_t
// TODO: maybe increase
#define MAX_USB_DEVICES 16
static usb_device_t* g_devices[MAX_USB_DEVICES];
static IUSBDRIVER* g_bound_drivers[MAX_USB_DEVICES * 16];
static usb_core_interface_driver_entry_t g_registered_drivers[MAX_USB_DEVICES * 16];
static u16 g_registered_driver_count = 0;

typedef struct {
    usb_device_t* dev;
    xhci_driver_t* driver;
    xhci_device_t* xdev;
} finalization_ticket_t;

static b8 match_interface(const usb_core_interface_match_t match, const usb_interface_t* iface) {
    if (match.interface_class != USB_MATCH_ANY && match.interface_class != iface->interface_class) {
        return false;
    }

    if (match.interface_subclass != USB_MATCH_ANY && match.interface_subclass != iface->interface_subclass) {
        return false;
    }

    if (match.interface_protocol != USB_MATCH_ANY && match.interface_protocol != iface->interface_protocol) {
        return false;
    }

    return true;
}

static usb_device_t* build_usb_device(xhci_driver_t* hcd, xhci_device_t* xdev, const usb_device_descriptor_t* desc) {
    usb_device_t* dev = (usb_device_t*)kmalloc(sizeof(usb_device_t));
    if (dev == NULL) {
        xassert(false, "")
        return NULL;
    }

    dev->vid = desc->idVendor;
    dev->pid = desc->idProduct;
    dev->bcd_usb = desc->bcdUsb;
    dev->device_class = desc->bDeviceClass;
    dev->device_subclass = desc->bDeviceSubClass;
    dev->device_protocol = desc->bDeviceProtocol;
    dev->speed = xdev->speed;
    dev->config_value = 0;
    dev->num_interfaces = xdev->num_interfaces;
    dev->is_hub = false; // TODO: XHCI_DEVICE_IS_HUB(*xdev);
    dev->hub_num_ports = 0; // TODO: xdev->hub_num_ports();
    dev->hcd = hcd;
    dev->hcd_device = xdev;
    dev->active_driver_count = 0;
    dev->disconnect_pending = false;
    dev->hcd_teardown_complete = false;
    dev->finalize_started = false;

    xdev->core_device = dev;

    for (u8 i = 0; i < xdev->num_interfaces; i++) {
        const xhci_interface_info_t* xi = &xdev->interfaces[i];
        usb_interface_t* iface = &dev->interfaces[i];
        iface->interface_number = xi->interface_number;
        iface->alternate_setting = xi->alternate_setting;
        iface->interface_class = xi->interface_class;
        iface->interface_subclass = xi->interface_subclass;
        iface->interface_protocol = xi->interface_protocol;
        iface->hid_report_desc_length = xi->hid_report_desc_length;
        iface->num_endpoints = xi->num_endpoints;

        for (u8 j = 0; j < xi->num_endpoints && j < 16; j++) {
            xhci_endpoint_t* xep = xdev->endpoints[xi->endpoint_dcis[j]];
            if (!xep) {
                continue;
            }

            usb_endpoint_t* ep = &iface->endpoints[j];
            ep->address = xep->endpoint_addr;
            ep->transfer_type = XHCI_ENDPOINT_TRANSFER_TYPE(*xep);
            ep->max_packet_size = xep->max_packet_size;
            ep->interval = xep->interval;
        }
    }

    return dev;
}

static IUSBDRIVER* take_bound_driver_for_device(u16 drv_idx, usb_device_t* dev) {
    if (!dev || drv_idx >= MAX_USB_DEVICES * 16) {
        return NULL;
    }

    IUSBDRIVER* drv = NULL;
    IUSBDRIVER* current = g_bound_drivers[drv_idx];
    if (current && current->bound_device == dev) {
        drv = current;
        g_bound_drivers[drv_idx] = NULL;
    }

    return drv;
}

void usb_core_device_configured(xhci_driver_t* driver, xhci_device_t* xdev, const usb_device_descriptor_t* desc) {
    u8 slot_id = xdev->slot;

    usb_device_t* dev = build_usb_device(driver, xdev, desc);
    if (!dev) {
        printf("[USB CORE]: Failed to allocate usb_device_t for slot %u\n", slot_id);
        return;
    }

    g_devices[slot_id] = dev;

    for (u8 i = 0; i < dev->num_interfaces; ++i) {
        usb_interface_t* iface = &dev->interfaces[i];

        for (u16 driver_index = 0; driver_index < g_registered_driver_count; ++i) {
            usb_core_interface_driver_entry_t* reg = &g_registered_drivers[i];
            if (!match_interface(reg->match, iface)) { continue; }

            IUSBDRIVER* drv = reg->create(dev, iface);
            if (!drv) { continue; }

            if (drv->probe((void*)drv, dev, iface)) { 
                kfree(drv);
                continue;
            }

            // TODO: create kernel task here

            drv->bound_device = dev;
            drv->bound_slot_id = slot_id;
            drv->bound_interface_index = i;

            ++dev->active_driver_count;

            u16 drv_idx = (u16)(slot_id) * 16u + (u16)i;
            if (!(drv_idx >= MAX_USB_DEVICES * 16)) {
                xassert(false, "");
                g_bound_drivers[drv_idx] = drv;
            }

            printf("[USB CORE]: Bound %s to interface %u (class=0x%x)\n", drv->name, iface->interface_number, iface->interface_class);

            break;
        }
    }
}

// TODO: left off here
void usb_core_device_disconnected(xhci_driver_t* driver, xhci_device_t* xdev) {
    if (!xdev) { return; }

    usb_device_t* dev = xdev->core_device;
    if (!dev) { return; }

    dev->disconnect_pending = true;

    u8 slot_id = xdev->slot;
    for (u8 i = 0; i < dev->num_interfaces; ++i) {
        u16 drv_idx = (u16)(slot_id) * 16 + i;
        IUSBDRIVER* drv = take_bound_driver_for_device(drv_idx, dev);
        if (!drv) { continue; }

        drv->disconnect(drv);
    }
}

void usb_core_device_finalized_disconnected_device(xhci_driver_t* driver, xhci_device_t* xdev) {
    if (!xdev) { return; }

    usb_device_t* dev = xdev->core_device;
    if (!dev) {
        xdev->core_device = NULL;
        if (driver) {
            xhci_driver_release_disconnected_device(driver, xdev);
        }
        return;
    }

    dev->hcd_teardown_complete = true;
    // TODO: THREADING HERE
}

void usb_core_register_driver(const char* name, usb_core_interface_match_t match, usb_core_interface_driver_factory_func factory) {
    usb_core_interface_driver_entry_t entry;
    entry.name = name;
    entry.create = factory;
    entry.match = match;

    g_registered_drivers[g_registered_driver_count] = entry;
    ++g_registered_driver_count;
}