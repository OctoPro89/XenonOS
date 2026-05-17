#include <drivers/usb/xhci_regs.h>
#include <drivers/usb/xhci_common.h>

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
