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

    endpoint.async_state = (xhci_endpoint_async_state_t*)kmalloc(sizeof(xhci_endpoint_async_state_t));
    if (!endpoint.async_state) {
        // TODO: throw error
        xassert(false, "");
        return endpoint;
    }

    endpoint.async_state->active_request = NULL;
    endpoint.async_state->pending_head = NULL;
    endpoint.async_state->pending_tail = NULL;
    endpoint.async_state->async_enabled = false;
    endpoint.async_state->disconnecting = false;
    endpoint.async_state->active_request_cancelled = false;
    endpoint.async_state->interrupt_in_stream.active = false;
    endpoint.async_state->interrupt_in_stream.closing = false;
    endpoint.async_state->interrupt_in_stream.payload_length = 0;
    endpoint.async_state->interrupt_in_stream.queue_depth = 0;
    endpoint.async_state->interrupt_in_stream.payloads = NULL;
    endpoint.async_state->interrupt_in_stream.payload_storage = NULL;
    endpoint.async_state->interrupt_in_stream.head = 0;
    endpoint.async_state->interrupt_in_stream.count = 0;
    endpoint.async_state->interrupt_in_stream.next_seq = 1;
    endpoint.async_state->interrupt_in_stream.dropped = 0;

    endpoint.dma_buffer = xhci_alloc_memory(PAGE_SIZE, XHCI_ENDPOINT_CONTEXT_ALIGNMENT, XHCI_ENDPOINT_CONTEXT_BOUNDARY); // TODO: alignment and boundary are definitely wrong
    if (((void*)endpoint.dma_buffer.virt) == NULL) {
        // TODO: throw error
        xassert(false, "");
        return endpoint;
    }

    return endpoint;
}

void xhci_endpoint_destroy(xhci_endpoint_t* ep) {
    if (ep->async_state) {
        if (ep->async_state->interrupt_in_stream.payload_storage) {
            kfree(ep->async_state->interrupt_in_stream.payload_storage);
            ep->async_state->interrupt_in_stream.payload_storage = NULL;
        }

        if (ep->async_state->interrupt_in_stream.payloads) {
            kfree(ep->async_state->interrupt_in_stream.payloads);
            ep->async_state->interrupt_in_stream.payloads = NULL;
        }

        kfree(ep->async_state);
        ep->async_state = NULL;
    }

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