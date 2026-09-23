#include <drivers/usb/xhci_endpoint.h>
#include <xlibc/stdlib.h>
#include <memory/paging.h>

// Convert USB endpoint descriptor attributes to xHCI endpoint type
static u8 usb_to_xhci_ep_type(u8 usb_type, b8 is_in) {
    switch (usb_type) {
        case 0: return XHCI_ENDPOINT_TYPE_CONTROL;
        case 1: return is_in ? XHCI_ENDPOINT_TYPE_ISOCHRONOUS_IN : XHCI_ENDPOINT_TYPE_ISOCHRONOUS_OUT;
        case 2: return is_in ? XHCI_ENDPOINT_TYPE_BULK_IN : XHCI_ENDPOINT_TYPE_BULK_OUT;
        case 3: return is_in ? XHCI_ENDPOINT_TYPE_INTERRUPT_IN : XHCI_ENDPOINT_TYPE_INTERRUPT_OUT;
        default: return XHCI_ENDPOINT_TYPE_INVALID;
    }
}

// Compute the xHCI Device Context Index from the USB endpoint address
// DCI = (endpoint_number * 2) + direction (0=OUT, 1=IN)
static u8 compute_dci(u8 endpoint_addr) {
    u8 ep_num = endpoint_addr & 0x0F;
    b8 is_in = (endpoint_addr & 0x80) != 0;
    return (u8)(ep_num * 2 + (is_in ? 1 : 0));
}

xhci_endpoint_t xhci_endpoint_init(u8 slot_id, const usb_endpoint_descriptor_t* desc) {
    xhci_endpoint_t endpoint;
    endpoint.endpoint_addr = desc->bEndpointAddress;
    endpoint.attributes = desc->bmAttributes;
    endpoint.max_packet_size = desc->wMaxPacketSize;
    endpoint.interval = desc->bInterval;
    endpoint.dci = compute_dci(endpoint.endpoint_addr);
    endpoint.xhc_ep_type = usb_to_xhci_ep_type(XHCI_ENDPOINT_TRANSFER_TYPE(endpoint), XHCI_ENDPOINT_IS_IN(endpoint));

    endpoint.ring = (xhci_transfer_ring_t*)kmalloc(sizeof(xhci_transfer_ring_t));
    if (!endpoint.ring) {
        // TODO: throw error
        xassert(false, "");
        return endpoint;
    }

    *endpoint.ring = xhci_transfer_ring_init(XHCI_TRANSFER_RING_TRB_COUNT, slot_id);

    endpoint.dma_buffer = xhci_alloc_memory(PAGE_SIZE, XHCI_ENDPOINT_CONTEXT_ALIGNMENT, XHCI_ENDPOINT_CONTEXT_BOUNDARY); // TODO: alignment and boundary are definitely wrong
    if (((void*)endpoint.dma_buffer.virt) == NULL) {
        // TODO: throw error
        xassert(false, "");
        return endpoint;
    }

    return endpoint;
}

void xhci_endpoint_destroy(xhci_endpoint_t* ep) {
    if (ep->dma_buffer.virt) {
        xhci_free_memory((void*)ep->dma_buffer.virt);
        ep->dma_buffer.virt = (vaddr_t)NULL;
        ep->dma_buffer.phys = (paddr_t)NULL;
    }

    if (ep->ring) {
        // TODO:
        // xhci_transfer_ring_destroy(ep->ring);
        kfree(ep->ring);
        ep->ring = NULL;
    }
}