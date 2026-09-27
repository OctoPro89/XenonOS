#include <drivers/usb/hid/hid_driver.h>
#include <drivers/usb/hid/hid_keyboard_handler.h>
#include <drivers/usb/usb_descriptors.h>
#include <memory/paging.h>
#include <xlibc/xassert.h>
#include <xlibc/stdlib.h>
#include <xlibc/string.h>
#include <xlibc/stdio.h>

typedef struct {
    b8 keyboard;
    b8 mouse;
} report_capabilities_t;

static report_capabilities_t classify_report(const usb_hid_report_layout_t* layout, const usb_hid_input_report_info_t* report) {
    report_capabilities_t caps;
    memset(&caps, 0, sizeof(report_capabilities_t));
    const usb_hid_field_info_t* fields = usb_hid_report_fields(layout, report);
    if (!fields) {
        return caps;
    }

    u16 gd = (u16)USB_HID_USAGE_PAGE_GENERIC_DESKTOP;
    u16 kb = (u16)USB_HID_USAGE_PAGE_KEYBOARD;
    b8 has_x = false;
    b8 has_y = false;
    
    for (u16 i = 0; i < report->field_count; ++i) {
        const usb_hid_field_info_t* field = &fields[i];
        if (USB_HID_FIELD_INFO_IS_CONSTANT(*field)) { continue; }

        if (field->usage_page == gd) {
            if (field->usage == (u16)USB_HID_GENERIC_DESKTOP_USAGE_X_AXIS) {
                has_x = true;
            }

            if (field->usage == (u16)USB_HID_GENERIC_DESKTOP_USAGE_Y_AXIS) {
                has_y = true;
            }
        }

        if (field->usage_page == kb && (!(USB_HID_FIELD_INFO_IS_VARIABLE(*field)) || (!hid_keyboard_handler_is_modifier_usage(field->usage)))) {
            caps.keyboard = true;
        }
    }

    caps.mouse = has_x && has_y;
    return caps;
}

static const usb_endpoint_t* hid_driver_find_interrupt_in_endpoint(const hid_driver_t* self) {
    for (u8 i = 0; i < self->iface->num_endpoints; ++i) {
        const usb_endpoint_t* ep = &self->iface->endpoints[i];
        if (ep->transfer_type == 3 && USB_ENDPOINT_IS_IN(*ep)) { // 3 == interrupt
            return ep;
        }
    }

    return NULL;
}

static u32 hid_driver_max_input_report_bytes(const hid_driver_t* self, const usb_endpoint_t* ep) {
    if (self->layout.max_input_report_bytes == 0) {
        return 0;
    }

    if (self->layout.max_input_report_bytes > ep->max_packet_size) {
        printf("[HID DRIVER]: Input report wire length %u exceeds EP 0x%x max packet size %u\n", self->layout.max_input_report_bytes, ep->address, ep->max_packet_size);
        return 0;
    }

    if (self->layout.max_input_report_bytes > PAGE_SIZE) {
        printf("[HID DRIVER]: Input report wire length %u exceeds stream limit %u\n", self->layout.max_input_report_bytes, (u32)PAGE_SIZE);
        return 0;
    }

    return self->layout.max_input_report_bytes;
}

static void hid_driver_destroy_bindings(hid_driver_t* self) {
    if (self->bindings) {
        for (u16 i = 0; i < self->binding_count; ++i) {
            if (self->bindings[i].handler) {
                kfree(self->bindings[i].handler);
                self->bindings[i].handler = NULL;
            }
        }

        kfree(self->bindings);
        self->bindings = NULL;
    }

    self->binding_count = 0;
}

static b8 hid_driver_create_handlers(hid_driver_t* self) {
    hid_driver_destroy_bindings(self);

    u16 binding_count = 0;
    for (u16 i = 0; i < self->layout.num_input_reports; ++i) {
        report_capabilities_t caps = classify_report(&self->layout, &self->layout.input_reports[i]);
        if (caps.keyboard) {
            ++binding_count;
        }

        if (caps.mouse) {
            ++binding_count;
        }
    }

    if (binding_count == 0) { return false; }

    self->bindings = (hid_handler_binding_t*)kmalloc(binding_count * sizeof(hid_handler_binding_t));
    if (!self->bindings) {
        xassert(false, "");
        return false;
    }

    u16 slot = 0;
    self->binding_count = 0;
    for (u16 i = 0; i < self->layout.num_input_reports; ++i) {
        const usb_hid_input_report_info_t* report = &self->layout.input_reports[i];
        report_capabilities_t caps = classify_report(&self->layout, report);

        if (caps.keyboard) {
            hid_keyboard_handler_t* handler = (hid_keyboard_handler_t*)kmalloc(sizeof(hid_keyboard_handler_t));
            if (handler) {
                *handler = hid_keyboard_handler_create();
                if (!handler->init(handler, &self->layout, report)) {
                    if (handler) { kfree(handler); }
                }
                else {
                    self->bindings[slot++] = (hid_handler_binding_t){ .report_id = report->report_id, .binding_kind = HID_BINDING_KIND_KEYBOARD, (IHIDHANDLER*)handler };
                    self->binding_count = slot;
                }
            }
        }

        if (caps.mouse) {
            // TODO:
            /*
            hid_mouse_handler_t* handler = (hid_mouse_handler_t*)kmalloc(sizeof(hid_mouse_handler_t));
            if (handler) {
                *handler = hid_mouse_handler_create();
                if (!handler->init(handler, &self->layout, report)) {
                    if (handler) { kfree(handler); }
                }
                else {
                    self->bindings[slot++] = (hid_handler_binding_t){ .report_id = report->report_id, .binding_kind = HID_BINDING_KIND_MOUSE, handler };
                    self->binding_count = slot;
                }
            }
            */
        }
    }

    self->binding_count = slot;
    return self->binding_count > 0 ? true : false;
}

static void hid_driver_apply_idle_policy(hid_driver_t* self, usb_device_t* dev) {
    for (u16 i = 0; i < self->binding_count; ++i) {
        if (!self->bindings[i].handler || self->bindings[i].binding_kind != HID_BINDING_KIND_KEYBOARD) {
            continue;
        }

        u8 report_id = self->layout.uses_report_ids ? self->bindings[i].report_id : 0;
        b8 already_sent = false;
        for (u16 j = 0; j < i; ++j) {
            if (self->bindings[j].handler && self->bindings[j].binding_kind == HID_BINDING_KIND_KEYBOARD && self->bindings[j].report_id == self->bindings[i].report_id) {
                already_sent = true;
                break;
            }
        }

        if (already_sent) { continue; }

        b8 rc = usb_control_transfer(dev, USB_REQTYPE_DIR_OUT | USB_REQTYPE_TYPE_CLASS | USB_REQTYPE_RECIP_INTERFACE, USB_HID_REQUEST_SET_IDLE, report_id, self->iface->interface_number, NULL, 0);
        if (!rc) {
            printf("[HID DRIVER]: SET_IDLE failed for interface %u report %u\n", self->iface->interface_number, report_id);
        }
    }
}

static void hid_driver_dispatch_report(hid_driver_t* self, u8 report_id, const u8* data, u32 length) {
    for (u16 i = 0; i < self->binding_count; ++i) {
        if (self->bindings[i].report_id == report_id && self->bindings[i].handler) {
            self->bindings[i].handler->on_report(self->bindings[i].handler, data, length);
        }
    }
}

// INTERFACE IMPLEMENTATIONS
static void hid_driver_destroy(void* _self) {
    hid_driver_t* self = (hid_driver_t*)_self;
    if (self->stream) {
        // TODO:
        self->stream = NULL;
    }

    hid_driver_destroy_bindings(self);
    usb_hid_report_layout_destroy(&self->layout);
}

static b8 hid_driver_probe(void* _self, usb_device_t* dev, usb_interface_t* iface) {
    hid_driver_t* self = (hid_driver_t*)_self;
    // TODO: assuming this is an alright place to put iface
    self->iface = iface;
    const usb_endpoint_t* ep = hid_driver_find_interrupt_in_endpoint(self);
    if (!ep) {
        printf("[HID DRIVER]: No interrupt IN endpoint found\n");
        return false;
    }

    // GET_REPORT_DESCRIPTOR, HID report descriptor is at interface level
    b8 rc = 0;
    u16 desc_len = iface->hid_report_desc_length != 0 ? iface->hid_report_desc_length : 256;
    if (desc_len > 4096) {
        printf("[HID DRIVER]: Report descriptor too large (%u bytes)", desc_len);
        return false;
    }

    u8* desc_buf = (u8*)kmalloc(desc_len * sizeof(u8));
    if (!desc_buf) {
        printf("[HID DRIVER]: Failed to allocate report descriptor buffer\n");
        return false;
    }

    memset(desc_buf, 0, desc_len);

    rc = usb_control_transfer(dev, USB_REQTYPE_DIR_IN | USB_REQTYPE_TYPE_STANDARD | USB_REQTYPE_RECIP_INTERFACE, USB_REQUEST_GET_DESCRIPTOR,
        USB_DESCRIPTOR_REQUEST(USB_DESCRIPTOR_HID_REPORT, 0),
        iface->interface_number,
        desc_buf, desc_len);
    
    if (!rc) {
        printf("[HID DRIVER]: Failed to get report descriptor\n");
        kfree(desc_buf);
        return false;
    }

    rc = usb_hid_parse_report_descriptor(desc_buf, desc_len, &self->layout);
    kfree(desc_buf);
    if (!rc) {
        printf("[HID DRIVER]: Failed to parse report descriptor\n");
        return false;
    }

    printf("[HID DRIVER]: Parsed %u input fields across %u input reports%s\n", self->layout.num_fields, self->layout.num_input_reports, self->layout.uses_report_ids ? " with report IDs" : "");

    rc = hid_driver_create_handlers(self);
    if (!rc) {
        printf("[HID DRIVER]: No supported report-protocol handlers for interface %u\n", iface->interface_number);
        return false;
    }

    if (iface->interface_subclass == 0x01) {
        if (iface->interface_protocol != 0x01 && iface->interface_protocol != 0x02) {
            printf("[HID DRIVER]: Unsupported boot-protocol HID interface %u (protocol=%u)\n", iface->interface_number, iface->interface_protocol);
            return false;
        }

        rc = usb_control_transfer(dev, USB_REQTYPE_DIR_OUT | USB_REQTYPE_TYPE_CLASS | USB_REQTYPE_RECIP_INTERFACE, USB_HID_REQUEST_SET_PROTOCOL, 1, iface->interface_number, NULL, 0);
        if (!rc) {
            printf("[HID DRIVER]: SET_PROTOCOL(REPORT) failed for interface %u\n", iface->interface_number);
            return false;
        }
    }
    else if (iface->interface_subclass != 0x00) {
        printf("[HID DRIVER]: Unsupported HID subclass %u on interface %u\n", iface->interface_subclass, iface->interface_number);
        return false;
    }

    // only apply SET_IDLE to keyboard reports, and target those report IDs
    // individualy when the interface uses Report IDs
    hid_driver_apply_idle_policy(self, dev);

    self->payload_length = hid_driver_max_input_report_bytes(self, ep);
    if (self->payload_length == 0) {
        printf("[HID DRIVER]: Invalid input report payload length\n");
        return false;
    }

    rc = usb_transfer_open_interrupt_in_stream(dev, ep->address, self->payload_length, &self->stream);
    if (!rc) {
        printf("[HID DRIVER]: Failed to open interrupt stream on EP %x\n", ep->address);
        return false;
    }

    printf("[HID DRIVER]: Opened interrupt stream on EP 0x%x (%u byte payload)\n", ep->address, self->payload_length);
    return true;
}

void hid_driver_run(void* _self) {
    hid_driver_t* self = (hid_driver_t*)_self;
    const usb_endpoint_t* ep = hid_driver_find_interrupt_in_endpoint(self);
    if (!self->stream || !ep || self->payload_length == 0) {
        printf("[HID DRIVER]: hid_driver_run() called without an active interrupt stream\n");
        return;
    }

    u8* report_buf = (u8*)kmalloc(self->payload_length);
    if (!report_buf) {
        printf("[HID DRIVER]: Failed to allocate report buffer\n");
        return;
    }

    printf("[HID DRIVER]: Starting interrupt stream loop on EP 0x%x (%u bytes)\n", ep->address, self->payload_length);

    while (!self->disconnected) {
        u32 actual = 0;
        memset(report_buf, 0, self->payload_length);
        b8 rc = usb_transfer_read_interrupt_in_stream(self->stream, report_buf, self->payload_length, &actual);
        if (!rc) {
            if (self->disconnected) { break; }

            printf("[HID DRIVER]: Interrupt stream read failed (%d)\n", rc);
            break;
        }

        u8 report_id = 0;
        const u8* report_data = report_buf;
        u32 report_length = actual;

        if (self->layout.uses_report_ids) {
            if (actual == 0) {
                printf("[HID DRIVER]: Dropped empty report missing report ID\n");
                continue;
            }

            report_id = report_buf[0];
            report_data = report_buf + 1;
            report_length = actual - 1;
        }

        if (!usb_hid_find_input_report(&self->layout, report_id)) {
            printf("[HID DRIVER]: Unknown input report ID %u\n", report_id);
            continue;
        }

        hid_driver_dispatch_report(self, report_id, report_data, report_length);
    }

    kfree(report_buf);
    if (self->stream) {
        usb_transfer_close_interrupt_in_stream(self->stream);
        self->stream = NULL;
    }
}

void hid_driver_disconnect(void* _self) {
    hid_driver_t* self = (hid_driver_t*)_self;
    self->disconnected = true;
}

IUSBDRIVER* hid_driver_factory(usb_device_t* dev, usb_interface_t* iface) {
    hid_driver_t* drv = kmalloc(sizeof(hid_driver_t));
    xassert(drv, "Failed to allocate driver!");
    drv->name = "USB-HID DRIVER";
    // INTERFACE ASSIGN
    drv->finalize_create = NULL;
    drv->destroy = hid_driver_destroy;
    drv->probe = hid_driver_probe;
    drv->run = hid_driver_run;
    drv->disconnect = hid_driver_disconnect;
    return (IUSBDRIVER*)drv;
}