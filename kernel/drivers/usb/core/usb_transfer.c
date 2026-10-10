#include <drivers/usb/core/usb_transfer.h>
#include <drivers/usb/xhci_device.h>
#include <drivers/usb/xhci.h>
#include <xlibc/xassert.h>
#include <xlibc/string.h>
#include <xlibc/stdlib.h>
#include <time/time.h>

usb_transfer_request_t usb_transfer_request_init(u8 endpoint_addr, void* buffer, u32 requested_length, u32 flags) {
    usb_transfer_request_t req;
    memset(&req, 0, sizeof(usb_transfer_request_t));
    req.status = USB_TRANSFER_STATUS_INVALID;
    req.endpoint_addr = endpoint_addr;
    req.buffer = buffer;
    req.requested_length = requested_length;
    req.flags = flags;
    spin_lock_init(&req.lock);
    wait_queue_init(&req.complete_wq);

    return req;
}

b8 usb_transfer_request_submit_async(usb_device_t* dev, usb_transfer_request_t* req) {
    if (!dev || !dev->hcd || !dev->hcd_device) {
        xassert(false, "");
        return false;
    }

    xhci_driver_t* drv = (xhci_driver_t*)dev->hcd;
    xhci_device_t* xdev = (xhci_device_t*)dev->hcd_device;
    return xhci_driver_usb_submit_transfer_async(drv, xdev, req);
}

usb_transfer_status_t usb_transfer_request_await(usb_transfer_request_t* req) {
    u64 flags = 0;
    spin_lock_irqsave(&req->lock, &flags);
    while (req->pending) {
        flags = task_wait(&req->complete_wq, &req->lock, flags);
    }
    spin_unlock_irqrestore(&req->lock, flags);

    return req->status;
}

void usb_transfer_request_cancel(usb_device_t* dev, usb_transfer_request_t* req) {
    if (!dev || !dev->hcd || !dev->hcd_device) {
        xassert(false, "");
        return;
    }

    xhci_driver_t* drv = (xhci_driver_t*)dev->hcd;
    xhci_device_t* xdev = (xhci_device_t*)dev->hcd_device;
    return xhci_driver_usb_cancel_transfer(drv, xdev, req);
}

b8 usb_transfer_open_interrupt_in_stream(usb_device_t* dev, u8 endpoint_addr, u32 payload_length, usb_interrupt_in_stream_t** out_stream) {
    if (!out_stream) { return false; }

    *out_stream = NULL;

    if (!dev || !dev->hcd || !dev->hcd_device) {
        xassert(false, "");
        return false;
    }

    xhci_driver_t* drv = (xhci_driver_t*)dev->hcd;
    xhci_device_t* xdev = (xhci_device_t*)dev->hcd_device;
    b8 rc = xhci_driver_usb_open_interrupt_in_stream(drv, xdev, endpoint_addr, payload_length);
    if (!rc) { return false; }

    usb_interrupt_in_stream_t* stream = (usb_interrupt_in_stream_t*)kmalloc(sizeof(usb_interrupt_in_stream_t));
    if (!stream) {
        xhci_driver_usb_close_interrupt_in_stream(drv, xdev, endpoint_addr);
        return false;
    }

    stream->dev = dev;
    stream->endpoint_addr = endpoint_addr;
    *out_stream = stream;
    return true;
}

b8 usb_transfer_read_interrupt_in_stream(usb_interrupt_in_stream_t* stream, void* buffer, u32 buffer_len, u32* out_length) {
    if (!stream || !stream->dev || !stream->dev->hcd || !stream->dev->hcd_device) {
        xassert(false, "");
        return false;
    }

    xhci_driver_t* drv = (xhci_driver_t*)stream->dev->hcd;
    xhci_device_t* xdev = (xhci_device_t*)stream->dev->hcd_device;
    return xhci_driver_usb_read_interrupt_in_stream(drv, xdev, stream->endpoint_addr, buffer, buffer_len, out_length);
}

void usb_transfer_close_interrupt_in_stream(usb_interrupt_in_stream_t* stream) {
    if (!stream) { return; }

    if (stream->dev && stream->dev->hcd && stream->dev->hcd_device) {
        xhci_driver_t* drv = (xhci_driver_t*)stream->dev->hcd;
        xhci_device_t* xdev = (xhci_device_t*)stream->dev->hcd_device;
        xhci_driver_usb_close_interrupt_in_stream(drv, xdev, stream->endpoint_addr);
    }

    kfree(stream);
}

b8 usb_control_transfer(usb_device_t* device, u8 request_type, u8 request, u16 value, u16 index, void* data, u16 length) {
    if (!device || !device->hcd || !device->hcd_device) {
        xassert(false, "");
        return false;
    }

    xhci_driver_t* drv = (xhci_driver_t*)device->hcd;
    xhci_device_t* xdev = (xhci_device_t*)device->hcd_device;
    return xhci_driver_usb_control_transfer(drv, xdev, request_type, request, value, index, data, length);
}

usb_transfer_status_t usb_interrupt_transfer(usb_device_t* device, u8 endpoint_addr, void* buffer, u32 length) {
    if (!device || !device->hcd || !device->hcd_device || (!buffer && length > 0)) {
        xassert(false, "");
        return USB_TRANSFER_STATUS_INVALID; // not sure what to return
    }

    if ((endpoint_addr & 0x80u) != 0) {
        memset(buffer, 0, length);
    }

    usb_transfer_request_t req = usb_transfer_request_init(endpoint_addr, buffer, length, (endpoint_addr & 0x80u) ? USB_TRANSFER_FLAGS_ALLOW_SHORT : 0);
    b8 rc = usb_transfer_request_submit_async(device, &req);
    if (!rc) {
        xassert(false, "");
        return USB_TRANSFER_STATUS_INVALID;
    }

    usb_transfer_status_t status = usb_transfer_request_await(&req);
    if (status == USB_TRANSFER_STATUS_OK || status == USB_TRANSFER_STATUS_SHORT_PACKET) { // TODO: check
        return USB_TRANSFER_STATUS_OK;
    }

    return status;
}

b8 usb_bulk_transfer(usb_device_t* device, u8 endpoint_addr, void* buffer, u32 length) {
    if (!device || !device->hcd || !device->hcd_device || (!buffer && length > 0)) {
        xassert(false, "");
        return false;
    }

    xhci_driver_t* drv = (xhci_driver_t*)device->hcd;
    xhci_device_t* xdev = (xhci_device_t*)device->hcd_device;
    return xhci_driver_usb_submit_transfer(drv, xdev, endpoint_addr, buffer, length);
}