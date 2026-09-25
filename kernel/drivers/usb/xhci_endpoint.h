#pragma once
#include <drivers/usb/xhci_common.h>
#include <drivers/usb/xhci_rings.h>
#include <drivers/usb/xhci_trb.h>
#include <drivers/usb/usb_descriptors.h>

typedef struct {
    u8 endpoint_addr;
    u8 attributes;
    u16 max_packet_size;
    u8 interval;
    u8 dci; // Device Context Index (1-31)
    u8 xhc_ep_type; // xHCI endpoint type for context programming

    xhci_transfer_ring_t* ring;

    dma_region_t dma_buffer;

    b8 completed;
    xhci_transfer_completion_trb_t result;
} xhci_endpoint_t;

#define XHCI_ENDPOINT_TRANSFER_TYPE(x) ((u8)((x).attributes & 0x03))
#define XHCI_ENDPOINT_IS_IN(x) ((b8)(((x).endpoint_addr & 0x80) != 0))
#define XHCI_ENDPOINT_NUM(x) ((u8)((x).endpoint_addr & 0x0F))

xhci_endpoint_t xhci_endpoint_init(u8 slot_id, const usb_endpoint_descriptor_t* desc);
void xhci_endpoint_destroy(xhci_endpoint_t* ep);