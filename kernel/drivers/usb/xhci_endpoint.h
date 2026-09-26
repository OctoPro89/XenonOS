#pragma once
#include <drivers/usb/xhci_common.h>
#include <drivers/usb/xhci_rings.h>
#include <drivers/usb/xhci_trb.h>
#include <drivers/usb/usb_descriptors.h>
#include <drivers/usb/core/usb_transfer.h>

typedef struct {
    u32 seq;
    u16 mfindex; // = 0xFFFF
    u16 len;
    u64 queued_t_us;
    u8* data;
} xhci_interrupt_in_payload_t;

#define XHCI_INTERRUPT_IN_PAYLOAD_CREATE() (xhci_interrupt_in_payload_t){ .seq = 0, .mfindex = 0xFFFF, .len = 0, .queued_t_us = 0, .data = NULL }

typedef struct {
    b8 active;
    b8 closing;
    u32 payload_length;
    u8 queue_depth;
    xhci_interrupt_in_payload_t* payloads;
    u8* payload_storage;
    u8 head;
    u8 count;
    u32 next_seq; // = 1
    u32 dropped;
} xhci_interrupt_in_stream_state_t;

typedef struct {
    b8 async_enabled;
    usb_transfer_request_t* active_request;
    usb_transfer_request_t* pending_head;
    usb_transfer_request_t* pending_tail;
    b8 disconnecting;
    b8 active_request_cancelled;
    xhci_interrupt_in_stream_state_t interrupt_in_stream;
} xhci_endpoint_async_state_t;

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
    xhci_endpoint_async_state_t* async_state;
} xhci_endpoint_t;

#define XHCI_ENDPOINT_TRANSFER_TYPE(x) ((u8)((x).attributes & 0x03))
#define XHCI_ENDPOINT_IS_IN(x) ((b8)(((x).endpoint_addr & 0x80) != 0))
#define XHCI_ENDPOINT_NUM(x) ((u8)((x).endpoint_addr & 0x0F))

xhci_endpoint_t xhci_endpoint_init(u8 slot_id, const usb_endpoint_descriptor_t* desc);
void xhci_endpoint_destroy(xhci_endpoint_t* ep);