#pragma once
#include <drivers/usb/xhci_regs.h>
#include <drivers/usb/xhci_mem.h>
#include <drivers/usb/xhci_trb.h>
#include <memory/memory_types.h>
#include <xlibc/xstddef.h>
#include <kernel.h>

// TODO: destroy functions

#define XHCI_RINGS_MAX_DEQUEUEABLE_EVENTS 512

typedef struct {
    size_t max_trb_count;   // number of valid TRBs in the ring including the LINK_TRB
    size_t enqueue_ptr;     // index in the ring where to enqueue the next TRB
    xhci_trb_t* trbs;       // base address of the ring buffer
    paddr_t physical_base;  // physical base of the ring
    u8 rcs_bit;             // ring cycle state
} xhci_command_ring_t;

xhci_command_ring_t xhci_command_ring_init(size_t max_trbs);
void xhci_command_ring_enqueue(xhci_command_ring_t* cmdring, xhci_trb_t* trb);

typedef struct __packed__  {
    u64 ring_segment_base_address; // base address of the event ring segment
    u32 ring_segment_size; // size of the event ring segment (only lower 16 bits are used)
    u32 rsvd;
} xhci_erst_entry_t;

typedef struct {
    volatile xhci_interrupter_registers_t* interrupter_regs;

    size_t segment_trb_count; // max TRBs allowed on the segment
    xhci_trb_t* trbs; // primary segment ring base
    paddr_t physical_base;
    xhci_erst_entry_t* segment_table; // event ring segment table base
    u64 dequeue_ptr; // event ring dequeue pointer / index
    u8 rcs_bit; // ring cycle state
} xhci_event_ring_t;

xhci_event_ring_t xhci_event_ring_init(size_t max_trbs, volatile xhci_interrupter_registers_t* interrupter);

static __hint_inline__ b8 xhci_event_ring_has_unprocessed_events(xhci_event_ring_t* er) {
    return er->trbs[er->dequeue_ptr].cycle_bit == er->rcs_bit;
}

/**
 * @param trbs This pointer-to-array should contain AT LEAST 512 entries to be safe
 */
void xhci_event_ring_dequeue_events(xhci_event_ring_t* er, xhci_trb_t** trbs, u64* out_dequeued);
void xhci_event_ring_flush_unprocessed_events(xhci_event_ring_t* er);
void xhci_event_ring_finish_procecssing(xhci_event_ring_t* er);

typedef struct {
    size_t max_trb_count; // Number of valid TRBs in the ring including the LINK_TRB
    size_t dequeue_ptr;   // Transfer ring consumer dequeue pointer
    size_t enqueue_ptr;   // Transfer ring producer enqueue pointer
    xhci_trb_t* trbs; // Base address of the ring buffer
    paddr_t physical_base;
    u8 rcs_bit; // Dequeue cycle state
    u8 doorbell_id; // ID of the doorbell associated with the ring
} xhci_transfer_ring_t;

xhci_transfer_ring_t xhci_transfer_ring_init(size_t max_trbs, u8 doorbell_id);

paddr_t xhci_transfer_ring_get_enqueue_phys(xhci_transfer_ring_t* tr);
b8 xhci_transfer_ring_can_enqueue(xhci_transfer_ring_t* tr, size_t n);
void xhci_transfer_ring_enqueue(xhci_transfer_ring_t* tr, xhci_trb_t* trb);