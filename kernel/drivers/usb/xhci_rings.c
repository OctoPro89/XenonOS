#include <drivers/usb/xhci_rings.h>
#include <memory/paging.h>

xhci_command_ring_t xhci_command_ring_init(size_t max_trbs) {
    xhci_command_ring_t cmdring;
    cmdring.max_trb_count = max_trbs;
    cmdring.rcs_bit = 1;
    cmdring.enqueue_ptr = 0;

    const u64 ring_size = max_trbs * sizeof(xhci_trb_t);

    // create the command ring memory block
    dma_region_t trbs_region = xhci_alloc_memory(ring_size, XHCI_COMMAND_RING_SEGMENTS_ALIGNMENT, XHCI_COMMAND_RING_SEGMENTS_BOUNDARY);
    cmdring.trbs = (xhci_trb_t*)trbs_region.virt;

    cmdring.physical_base = trbs_region.phys;

    // set the last TRB as a link TRB to point back to the first TRB in the ring
    cmdring.trbs[cmdring.max_trb_count - 1].parameter = (u64)cmdring.physical_base;
    cmdring.trbs[cmdring.max_trb_count - 1].control = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_LINK_TRB_TC_BIT | cmdring.rcs_bit;
    return cmdring;
}

void xhci_command_ring_enqueue(xhci_command_ring_t* cmdring, xhci_trb_t* trb) {
    // adjust the TRB's cycle bit to the current RCS
    trb->cycle_bit = cmdring->rcs_bit;

    // insert the TRB into the ring
    cmdring->trbs[cmdring->enqueue_ptr] = *trb;

    // advance and possibly wrap the enqueue pointer if needed
    if (++cmdring->enqueue_ptr == cmdring->max_trb_count - 1) {
        // update the Link TRB to reflect the current cycle state including the TC flag
        cmdring->trbs[cmdring->max_trb_count - 1].control = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_LINK_TRB_TC_BIT | cmdring->rcs_bit;

        cmdring->enqueue_ptr = 0;
        cmdring->rcs_bit = !cmdring->rcs_bit;
    }
}

static void xhci_event_ring_update_erdp(xhci_event_ring_t* er) {
    u64 dequeue_addr = er->physical_base + (er->dequeue_ptr * sizeof(xhci_trb_t));
    er->interrupter_regs->erdp = dequeue_addr;
}

static xhci_trb_t* xhci_event_ring_dequeue_trb(xhci_event_ring_t* er) {
    if (er->trbs[er->dequeue_ptr].cycle_bit != er->rcs_bit) {
        xassert(false, "XHCI: Event Ring attempted to dequeue an invalid TRB!");
        return NULL;
    }

    // get the resulting TRB
    xhci_trb_t* ret = &er->trbs[er->dequeue_ptr];

    // advance and possibly wrap the dequeue pointer if needed
    if (++er->dequeue_ptr == er->segment_trb_count) {
        er->dequeue_ptr = 0;
        er->rcs_bit = !er->rcs_bit;
    }

    return ret;
}

xhci_event_ring_t xhci_event_ring_init(size_t max_trbs, volatile xhci_interrupter_registers_t* interrupter) {
    xhci_event_ring_t er;
    er.interrupter_regs = interrupter;
    er.segment_trb_count = max_trbs;
    er.rcs_bit = XHCI_CRCR_RING_CYCLE_STATE;
    er.dequeue_ptr = 0;
    
    // event ring will only use one segment
    const u64 segment_count = 1;

    const u64 segment_size = max_trbs * sizeof(xhci_trb_t);
    const u64 segment_table_size = segment_count * sizeof(xhci_erst_entry_t);
    
    // create the event ring segment memory blocks
    dma_region_t trbs_region = xhci_alloc_memory(segment_size, XHCI_EVENT_RING_SEGMENTS_ALIGNMENT, XHCI_EVENT_RING_SEGMENTS_BOUNDARY);
    er.trbs = (xhci_trb_t*)trbs_region.virt;

    // store the physical DMA base
    er.physical_base = trbs_region.phys;

    // create the event ring segment table
    dma_region_t segment_table_region = xhci_alloc_memory(segment_table_size, XHCI_EVENT_RING_SEGMENT_TABLE_ALIGNMENT, XHCI_EVENT_RING_SEGMENT_TABLE_BOUNDARY);
    er.segment_table = (xhci_erst_entry_t*)segment_table_region.virt;

    // construct the segment table entry
    xhci_erst_entry_t entry;
    entry.ring_segment_base_address = er.physical_base;
    entry.ring_segment_size = er.segment_trb_count;
    entry.rsvd = 0;

    // insert the constructed segment into the table
    er.segment_table[0] = entry;

    // configure the Event Ring Segment Table Size (ERSTSZ) register
    // NOTE: according to official spec must set these in this specific order!
    er.interrupter_regs->erstsz = 1;

    // initialize and set ERDP
    xhci_event_ring_update_erdp(&er);

    // write to ERSTBA register
    er.interrupter_regs->erstba = segment_table_region.phys;
    return er;
}

void xhci_event_ring_dequeue_events(xhci_event_ring_t* er, xhci_trb_t** trbs, u64* out_dequeued) {
    // process each event trb
    u64 i = 0;
    while (xhci_event_ring_has_unprocessed_events(er) && i < XHCI_RINGS_MAX_DEQUEUEABLE_EVENTS) {
        xhci_trb_t* trb = xhci_event_ring_dequeue_trb(er);
        if (!trb) { break; }
        if (trbs) {
            trbs[i] = trb;
        }

        ++i;
    }

    // update the ERDP register
    xhci_event_ring_update_erdp(er);

    // clear the EHB (Event Handler Busy) bit
    u64 erdp = er->interrupter_regs->erdp;
    erdp |= XHCI_ERDP_EHB;
    er->interrupter_regs->erdp = erdp;
    
    if (out_dequeued) { *out_dequeued = i; }
}

void xhci_event_ring_flush_unprocessed_events(xhci_event_ring_t* er) {
    xhci_event_ring_dequeue_events(er, NULL, NULL);
}