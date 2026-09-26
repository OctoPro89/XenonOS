#pragma once
#include <xlibc/xstdint.h>
#include <drivers/usb/core/usb_device.h>

typedef enum {
    USB_TRANSFER_STATUS_OK = 0,
    USB_TRANSFER_STATUS_SHORT_PACKET = 1,
    USB_TRANSFER_STATUS_CANCELLED = -1,
    USB_TRANSFER_STATUS_DEVICE_GONE = -2,
    USB_TRANSFER_STATUS_STALLED = -3,
    USB_TRANSFER_STATUS_IO_ERROR = -4,
    USB_TRANSFER_STATUS_INVALID = -5
} usb_transfer_status_t;

#define USB_TRANSFER_FLAGS_ALLOW_SHORT ((u32)1u << 0)

typedef struct usb_transfer_request usb_transfer_request_t;

typedef struct usb_transfer_request {
    u8 endpoint_addr;
    void* buffer;
    u32 requested_length;
    u32 actual_length;
    u32 flags;
    usb_transfer_status_t status;
    b8 pending;

    // internal linkage for per-endpoint software queues
    usb_transfer_request_t* next;
    void* hcd_private;
} usb_transfer_request_t;

typedef struct {
    usb_device_t* dev;
    u8 endpoint_addr;
} usb_interrupt_in_stream_t;

usb_transfer_request_t usb_transfer_request_init(u8 endpoint_addr, void* buffer, u32 requested_length, u32 flags);
b8 usb_transfer_request_submit_async(usb_device_t* dev, usb_transfer_request_t* req);
usb_transfer_status_t usb_transfer_request_await(usb_transfer_request_t* req);
void usb_transfer_request_cancel(usb_device_t* dev, usb_transfer_request_t* req);

b8 usb_transfer_open_interrupt_in_stream(usb_device_t* dev, u8 endpoint_addr, u32 payload_length, usb_interrupt_in_stream_t** out_stream);
b8 usb_transfer_read_interrupt_in_stream(usb_interrupt_in_stream_t* stream, void* buffer, u32 buffer_len, u32* out_length);
void usb_transfer_close_interrupt_in_stream(usb_interrupt_in_stream_t* stream);

/**
 * @note synchronous control transfer on EP0.
 * for IN (device-to-host): data is filled by the device
 * for OUT (host-to-device): data is sent to the device
 * direction is encoded in request_type bit 7 (0=OUT, 1=IN).
 */
b8 usb_control_transfer(usb_device_t* device, u8 request_type, u8 request, u16 value, u16 index, void* data, u16 length);

/**
 * @note synchronous interrupt transfer on a given endpoint.
 * direction (IN / OUT) is determined by the endpoint address bit 7.
 * blocks until the transfer is complete
 */
usb_transfer_status_t usb_interrupt_transfer(usb_device_t* device, u8 endpoint_addr, void* buffer, u32 length);

/**
 * @note synchronous bulk transfer on the given endpoint
 * direction (IN / OUT) is determined by the endpoint address bit 7.
 * blocks until the transfer is complete
 */
b8 usb_bulk_transfer(usb_device_t* device, u8 endpoint_addr, void* buffer, u32 length);