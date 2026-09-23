#pragma once

#include <drivers/usb/xhci_device_ctx.h>
#include <drivers/usb/xhci_trb.h>
#include <drivers/usb/xhci_rings.h>
#include <drivers/usb/xhci_endpoint.h>
#include <kernel.h>

#define XHCI_DEVICE_MAX_ENDPOINTS 31

typedef struct {
    u8 port; // 0-based port id
    u8 slot; // slot index in the xhci DCBAA
    u8 speed; // port speed
    b8 use_64byte_ctx;
    dma_region_t input_ctx;
    dma_region_t output_ctx;

    xhci_transfer_ring_t* ctrl_ring;
    dma_region_t ctrl_transfer_buffer;

    // non-control endpoints (DCI 2-31, index 0-1 unused)
    xhci_endpoint_t* endpoints[XHCI_DEVICE_MAX_ENDPOINTS + 1];

    b8 ctrl_completed;
    xhci_transfer_completion_trb_t ctrl_result;
} xhci_device_t;

xhci_device_t xhci_device_init(u8 port, u8 slot, u8 speed, b8 use_64byte_ctx);
void xhci_device_destroy(xhci_device_t* device);
xhci_input_control_context32_t* xhci_device_get_input_ctrl_ctx(xhci_device_t* device);
xhci_slot_context32_t* xhci_device_get_input_slot_ctx(xhci_device_t* device);
xhci_endpoint_context32_t* xhci_device_get_input_ctrl_ep_ctx(xhci_device_t* device);
xhci_endpoint_context32_t* xhci_device_get_input_ep_ctx(xhci_device_t* device, u8 endpoint_num);

// Copies data from the output device context into the input context
void xhci_device_sync_input_ctx(xhci_device_t* device);