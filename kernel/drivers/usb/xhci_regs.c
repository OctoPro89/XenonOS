#include <drivers/usb/xhci_regs.h>
#include <drivers/usb/xhci_common.h>
#include <xlibc/xstddef.h>

xhci_doorbell_manager_t xhci_doorbell_manager_init(vaddr_t base) {
    xhci_doorbell_manager_t dbm;
    dbm.doorbell_registers = (xhci_doorbell_register_t*)base;
    return dbm;
}

void xhci_doorbell_manager_ring_doorbell(xhci_doorbell_manager_t* db_manager, u8 doorbell, u8 target) {
    db_manager->doorbell_registers[doorbell].raw = (u32)target;
}

void xhci_doorbell_manager_ring_command_doorbell(xhci_doorbell_manager_t* db_manager) {
    xhci_doorbell_manager_ring_doorbell(db_manager, 0, XHCI_DOORBELL_TARGET_COMMAND_RING);
}

void xhci_doorbell_manager_ring_control_endpoint_doorbell(xhci_doorbell_manager_t* db_manager, u8 doorbell) {
    xhci_doorbell_manager_ring_doorbell(db_manager, doorbell, XHCI_DOORBELL_TARGET_CONTROL_EP_RING);
}

static xhci_extended_capability_t ext_nexts[16]; // TODO: could probably use a better solution but this works for now
static u8 ext_next_count = 0;

static void xhci_extended_capability_read_next_ext_caps(xhci_extended_capability_t* ext) {
    if (ext->entry.next) {
        volatile u32* next_cap_ptr = XHCI_NEXT_EXT_CAP_PTR(ext->base, ext->entry.next);
        ext_nexts[ext_next_count] = xhci_extended_capability_init(next_cap_ptr);
        ext->next = &ext_nexts[ext_next_count];
        ++ext_next_count;
    }
}

xhci_extended_capability_t xhci_extended_capability_init(volatile u32* cap_ptr) {
    xhci_extended_capability_t ext;
    ext.base = cap_ptr;   
    ext.entry.raw = *ext.base;
    ext.next = NULL;
    xhci_extended_capability_read_next_ext_caps(&ext);
    return ext;
}