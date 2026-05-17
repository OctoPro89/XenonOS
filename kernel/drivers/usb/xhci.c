#include <drivers/usb/xhci.h>
#include <drivers/usb/xhci_mem.h>
#include <drivers/usb/xhci_common.h>
#include <memory/paging.h>
#include <memory/heap.h>
#include <time/time.h>
#include <xlibc/stdio.h>
#include <xlibc/string.h>
#include <arch/x86_64/irq.h>

static xhci_trb_t* _event_poll_trbs[XHCI_RINGS_MAX_DEQUEUEABLE_EVENTS];

static xhci_command_completion_trb_t* xhci_driver_send_command_trb(xhci_driver_t* driver, xhci_trb_t* cmd_trb, u32 timeout_ms) {
    xhci_command_ring_enqueue(&driver->command_ring, cmd_trb);
    xhci_doorbell_manager_ring_command_doorbell(&driver->doorbell_manager);

    // wait for the IRQ and let host controller process the command
    u64 sleep_passed = 0;
    while (!driver->command_irq_completed) {
        usleep(10);
        sleep_passed += 10;

        if (sleep_passed > timeout_ms * 1000) { break; }
    }

    // ***important assumption*** //
    // only ONE command is being sent to the controller at a time
    xhci_command_completion_trb_t* completion_trb = driver->command_completion_event_count ? driver->command_completion_events[0] : NULL;
    
    driver->command_completion_event_count = 0;
    driver->command_irq_completed = 0;

    if (!completion_trb) {
        xassert(false, "Failed to find completion TRB for command!");
        return NULL;
    }

    if (completion_trb->completion_code != XHCI_TRB_COMPLETION_CODE_SUCCESS) {
        xassert(false, "Command TRB failed!");
        return NULL;
    }

    return completion_trb;
}

static void xhci_driver_acknowledge_irq(xhci_driver_t* driver, u8 interrupter) {
    // according to spec must clear EINT bit in USBSTS by writing '1' to it
    // NOTE: ***MUST*** do it here to avoid race-conditions
    driver->op_regs->usbsts = XHCI_USBSTS_EINT;

    // get the interrupter registers
    volatile xhci_interrupter_registers_t* interrupter_regs = &driver->runtime_regs->ir[interrupter];

    // read the current value of IMAN
    u32 iman = interrupter_regs->iman;

    // set the IP bit to '1' to clear it, preserve others including IE
    iman |= XHCI_IMAN_INTERRUPT_PENDING;

    // write back to IMAN
    interrupter_regs->iman = iman;
}

static void xhci_driver_process_events(xhci_driver_t* driver) {
    // poll the event ring for events
    u64 dequeued = 0;

    if (xhci_event_ring_has_unprocessed_events(&driver->event_ring)) {
        xhci_event_ring_dequeue_events(&driver->event_ring, _event_poll_trbs, &dequeued);
    }

    u8 command_completion_status = 0;
    for (u64 i = 0; i < dequeued; ++i) {
        xhci_trb_t* event = _event_poll_trbs[i];
        switch (event->trb_type) {
            case XHCI_TRB_TYPE_CMD_COMPLETION_EVENT: {
                command_completion_status = 1;
                driver->command_completion_events[driver->command_completion_event_count] = (xhci_command_completion_trb_t*)event;
                ++driver->command_completion_event_count;
            }
            default: { break; }
        }
    }

    driver->command_irq_completed = command_completion_status;
}

static void xhci_driver_irq_handler(struct regs* r, void* user_data) {
    xhci_driver_t* driver = (xhci_driver_t*)user_data;
    xhci_driver_process_events(driver);
    xhci_driver_acknowledge_irq(driver, 0);
}

static void xhci_driver_setup_dcbaa(xhci_driver_t* driver) {
    size_t dcbaa_size = sizeof(u64) * (driver->max_device_slots + 1);
    dma_region_t dcbaa_region = xhci_alloc_memory(dcbaa_size, XHCI_DEVICE_CONTEXT_ALIGNMENT, XHCI_DEVICE_CONTEXT_BOUNDARY);
    driver->dcbaa = (u64*)dcbaa_region.virt;
    driver->dcbaa_virtual_addresses = (u64*)kmalloc(driver->max_device_slots + 1);

    /*
        according to spec:
        get max scratchpad buffers, if it's > 0 then it should contain
        a pointer to the scratchpad buffer array.
    */
   
    // initialize scratchpad buffer array if needed
    if (driver->max_scratchpad_buffers > 0) {
        dma_region_t scratchpad_array_region = xhci_alloc_memory(driver->max_scratchpad_buffers * sizeof(u64), XHCI_DEVICE_CONTEXT_ALIGNMENT, XHCI_DEVICE_CONTEXT_BOUNDARY);
        u64* scratchpad_array = (u64*)scratchpad_array_region.virt;

        // create scratchpad pages
        for (u8 i = 0; i < driver->max_scratchpad_buffers; ++i) {
            dma_region_t scratchpad_region = xhci_alloc_memory(PAGE_SIZE,  XHCI_SCRATCHPAD_BUFFERS_ALIGNMENT, XHCI_SCRATCHPAD_BUFFERS_BOUNDARY);
            vaddr_t scratchpad = scratchpad_region.virt;
            scratchpad_array[i] = scratchpad_region.phys;
        }

        // set the first slot in the DCBAA to point to the scratchpad array
        driver->dcbaa[0] = scratchpad_array_region.phys;
        driver->dcbaa_virtual_addresses[0] = (vaddr_t)scratchpad_array;
    }

    // set dcbaa pointer in the operational registers
    driver->op_regs->dcbaap = dcbaa_region.phys;
}

static b8 xhci_driver_start_host_controller(xhci_driver_t* driver) {
    // ensure USBCMD bit for RUN/STOP is properly set
    u32 usbcmd = driver->op_regs->usbcmd;
    usbcmd |= XHCI_USBCMD_RUN_STOP;
    usbcmd |= XHCI_USBCMD_INTERRUPTER_ENABLE;
    driver->op_regs->usbcmd = usbcmd;

    // ensure the controller transitions out of the halted state
    const int max_retries = 1000;
    int retries = 0;

    while (driver->op_regs->usbsts & XHCI_USBSTS_HCH) {
        if (retries++ >= max_retries) {
            // timeout: controller failed to start
            return false;
        }

        msleep(1); // poll every 1 ms
    }

    // veryify CNR (Controller Not Ready) bit is clear
    if (driver->op_regs->usbsts & XHCI_USBSTS_CNR) {
        return false; // controller not ready
    }

    // controller started successfully
    return true;
}

static void xhci_driver_configure_operational_registers(xhci_driver_t* driver) {
    // enable device notifications
    driver->op_regs->dnctrl = 0xFFFF;

    // configure the usbconfig field
    driver->op_regs->config = (u32)driver->max_device_slots;

    xhci_driver_setup_dcbaa(driver);

    // setup the command ring and write CRCR
    driver->command_ring = xhci_command_ring_init(XHCI_COMMAND_RING_TRB_COUNT);
    driver->op_regs->crcr = driver->command_ring.physical_base | driver->command_ring.rcs_bit;
}

static void xhci_driver_configure_runtime_registers(xhci_driver_t* driver) {
    // get the primary interrupter register, NOTE: MUST be volatile
    volatile xhci_interrupter_registers_t* interrupter_regs = &driver->runtime_regs->ir[0];

    // enable interrupts
    u32 iman = interrupter_regs->iman;
    iman |= XHCI_IMAN_INTERRUPT_ENABLE;
    interrupter_regs->iman = iman;

    // setup the event ring and write to interrupter registers to set ERSTSZ, ERSDP, and ERSTBA
    driver->event_ring = xhci_event_ring_init(XHCI_EVENT_RING_TRB_COUNT, interrupter_regs);

    // clear any pending interrupts for the primary interrupter
    xhci_driver_acknowledge_irq(driver, 0);
}

b8 xhci_driver_init_device(xhci_driver_t* driver) {
    pci_bar_t* bar = &driver->pci_device->bar[0];
    driver->xhc_base = xhci_map_mmio(bar->base, bar->size);

    pci_enable_bus_master(driver->pci_device);
    xhci_driver_parse_capability_registers(driver);

    if (!xhci_driver_reset_host_controller(driver)) { return false; }

    xhci_driver_configure_operational_registers(driver);
    xhci_driver_configure_runtime_registers(driver);

    // install irq handler
    driver->irq_vector = irq_alloc_vector();
    irq_register_handler(driver->irq_vector, &xhci_driver_irq_handler, driver); // do this first to avoid race condition
    if (!pci_enable_msix(driver->pci_device, driver->irq_vector)) {
        pci_enable_msi(driver->pci_device, driver->irq_vector);
    }

    driver->command_irq_completed = 0;
    driver->command_completion_event_count = 0;
    memset(driver->command_completion_events, 0, sizeof(xhci_command_completion_trb_t*) * XHCI_RINGS_MAX_DEQUEUEABLE_EVENTS);

    return true;
}

b8 xhci_driver_start_device(xhci_driver_t* driver) {
    // the controller should be all set up here so it can be started
    if (!xhci_driver_start_host_controller(driver)) {
        return false;
    }

    xhci_trb_t trb;
    memset(&trb, 0, sizeof(trb));
    trb.trb_type = XHCI_TRB_TYPE_ENABLE_SLOT_CMD;

    xhci_command_completion_trb_t* completion_trb = xhci_driver_send_command_trb(driver, &trb, 200);

    return true;
}

b8 xhci_driver_shutdown_device(xhci_driver_t* driver) {
    // TODO:
    return true;
}

void xhci_driver_parse_capability_registers(xhci_driver_t* driver) {
    driver->cap_regs = (volatile xhci_capability_registers_t*)driver->xhc_base;
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
    driver->op_regs = (volatile xhci_operational_registers_t*)(driver->xhc_base + (vaddr_t)driver->capability_regs_length);

    // update the base pointer to the runtime register set
    driver->runtime_regs = (volatile xhci_runtime_registers_t*)(driver->xhc_base + (vaddr_t)driver->cap_regs->rtsopff);

    driver->doorbell_manager = xhci_doorbell_manager_init(driver->xhc_base + driver->cap_regs->dboff);
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

    // set the HC Reset bit
    usbcmd = driver->op_regs->usbcmd;
    usbcmd |= XHCI_USBCMD_HCRESET;
    usbcmd = driver->op_regs->usbcmd = usbcmd;

    // wait for this bit and CNR bit to clear
    timeout = 1000;
    while (driver->op_regs->usbcmd & XHCI_USBCMD_HCRESET || driver->op_regs->usbsts & XHCI_USBSTS_CNR) {
        if (--timeout == 0) {
            return false; // host controller did not reset within 1000ms
        }

        msleep(1);
    }

    // give a little slack
    msleep(50);

    // check the defaults of the operational registers
    if (driver->op_regs->usbcmd != 0) { return false; }
    if (driver->op_regs->dnctrl != 0) { return false; }
    if (driver->op_regs->crcr != 0)   { return false; }
    if (driver->op_regs->dcbaap != 0) { return false; }
    if (driver->op_regs->config != 0) { return false; }

    return true;
}