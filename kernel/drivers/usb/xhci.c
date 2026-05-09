#include <drivers/usb/xhci.h>
#include <drivers/usb/xhci_mem.h>
#include <drivers/usb/xhci_common.h>
#include <xlibc/stdio.h>
#include <time/time.h>

b8 xhci_driver_init_device(xhci_driver_t* driver) {
    pci_bar_t* bar = &driver->pci_device->bar[0];
    driver->xhc_base = xhci_map_mmio(bar->base, bar->size);
    xhci_driver_parse_capability_registers(driver);
    return true;
}

b8 xhci_driver_start_device(xhci_driver_t* driver) {

}

b8 xhci_driver_shutdown_device(xhci_driver_t* driver) {

}

void xhci_driver_parse_capability_registers(xhci_driver_t* driver) {
    driver->cap_regs = (volatile xhci_capability_registers*)driver->xhc_base;
    driver->capability_regs_length = driver->cap_regs->caplength;

    driver->max_device_slots = XHCI_MAX_DEVICE_SLOTS(driver->cap_regs);
    driver->max_interrupters = XHCI_MAX_INTERRUPTERS(driver->cap_regs);
    driver->max_ports = XHCI_MAX_PORTS(driver->cap_regs);

    driver->isochronous_scheduling_threshold = XHCI_IST(driver->cap_regs);
    driver->erst_max = XHCI_ERST_MAX(driver->cap_regs);
    driver->max_scratchpad_buffers = XHCI_MAX_SCRATCHPAD_BUFFERS(driver->cap_regs);

    driver->sixty_four_bit_addressing_capability = XHCI_AC64(driver->cap_regs);
    driver->bandwidth_negotiation_capability = XHCI_BNC(driver->cap_regs);
    driver->sixty_four_byte_context_size = XHCI_CSZ(driver->cap_regs);
    driver->port_power_control = XHCI_PPC(driver->cap_regs);
    driver->port_indicators = XHCI_PIND(driver->cap_regs);
    driver->light_reset_capability = XHCI_LHRC(driver->cap_regs);
    driver->extended_capabilities_offset = XHCI_XECP(driver->cap_regs) * sizeof(u32);

    // update the base opinter to operational register set
    driver->op_regs = (volatile xhci_operational_registers*)(driver->xhc_base + (vaddr_t)driver->capability_regs_length);
}

void xhci_driver_log_capability_registers(xhci_driver_t* driver) {
    printf("XHCI Capability Registers (0x%p)\n", (u64)driver->cap_regs);
    printf("\tLength                : %i\n", driver->capability_regs_length);
    printf("\tMax Device Slots      : %i\n", driver->max_device_slots);
    printf("\tMax Interrupters      : %i\n", driver->max_interrupters);
    printf("\tMax Ports             : %i\n", driver->max_ports);
    printf("\tIST                   : %i\n", driver->isochronous_scheduling_threshold);
    printf("\tERST Max Size         : %i\n", driver->erst_max);
    printf("\tScratchpad Buffers    : %i\n", driver->max_scratchpad_buffers);
    printf("\t64-bit Addressing     : %s\n", driver->sixty_four_bit_addressing_capability ? "yes" : "no");
    printf("\tBandwidth Negotiation : %i\n", driver->bandwidth_negotiation_capability);
    printf("\t64-byte Context Size  : %s\n", driver->sixty_four_byte_context_size ? "yes" : "no");
    printf("\tPort Power Control    : %i\n", driver->port_power_control);
    printf("\tPort Indicators       : %i\n", driver->port_indicators);
    printf("\tLight Reset Available : %i\n", driver->light_reset_capability);
    printf("\n");
}

b8 xhci_driver_reset_host_controller(xhci_driver_t* driver) {
    // according to spec:
    // read full 32 bit value of cmd reg
    // in local var clear or set appropriate bits
    // write full 32 bit value in one write
    u32 usbcmd = driver->op_regs->usbcmd;
    usbcmd &= ~XHCI_USBCMD_RUN_STOP;
    driver->op_regs->usbcmd = usbcmd;

    // spin for up to 200ms for the HCHalted bit to be set
    u32 timeout = 200;
    while (!(driver->op_regs->usbsts & XHCI_USBSTS_HCH)) {
        if (--timeout == 0) {
            return false; // host controller did not halt within 200ms
        }

        msleep(1);
    }
}