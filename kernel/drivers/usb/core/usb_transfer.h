#pragma once
#include <xlibc/xstdint.h>
#include <drivers/usb/core/usb_device.h>

typedef struct {
    usb_device_t* dev;
    u8 endpoint_addr;
} usb_interrupt_in_stream_t;

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
b8 usb_interrupt_transfer(usb_device_t* device, u8 endpoint_addr, void* buffer, u32 length);

/**
 * @note synchronous bulk transfer on the given endpoint
 * direction (IN / OUT) is determined by the endpoint address bit 7.
 * blocks until the transfer is complete
 */
b8 usb_bulk_transfer(usb_device_t* device, u8 endpoint_addr, void* buffer, u32 length);