#include <drivers/usb/xhci.h>
#include <drivers/usb/xhci_mem.h>
#include <drivers/usb/xhci_common.h>
#include <drivers/usb/xhci_device_ctx.h>
#include <drivers/usb/usb_descriptors.h>
#include <memory/paging.h>
#include <memory/heap.h>
#include <time/time.h>
#include <xlibc/stdio.h>
#include <xlibc/string.h>
#include <arch/x86_64/irq.h>
#include <arch/x86_64/barrier.h>

static const char* usb_speed_strings[7] = {
    "Invalid",
    "Full Speed (12 Mbits/s - USB 2.0)",
    "Low Speed (1.5 Mbits/s - USB 2.0)",
    "High Speed (480 Mbits/s - USB 2.0)",
    "Super Speed (5 Gbits/s - USB 3.0)",
    "Super Speed Plus (10 Gbits/s - USB 3.1)",
    "Undefined"
};

static xhci_command_completion_trb_t* xhci_driver_send_command_trb(xhci_driver_t* driver, xhci_trb_t* cmd_trb, u32 timeout_ms);
static xhci_portsc_register_t xhci_driver_read_portsc_reg(xhci_driver_t* driver, u8 port_num);
static void xhci_driver_write_portsc_reg(xhci_driver_t* driver, xhci_portsc_register_t reg, u8 port_num);
static void xhci_driver_acknowledge_irq(xhci_driver_t* driver, u8 interrupter);
static void xhci_driver_acknowledge_portsc_changes(xhci_driver_t* driver, u8 port_index, u32 change_bits);
static void xhci_driver_teardown_device(xhci_driver_t* driver, u8 port_index);
static void xhci_driver_process_events(xhci_driver_t* driver);
static void xhci_driver_irq_handler(struct regs* r, void* user_data);
static void xhci_driver_setup_dcbaa(xhci_driver_t* driver);
static b8 xhci_driver_start_host_controller(xhci_driver_t* driver);
static void xhci_driver_configure_operational_registers(xhci_driver_t* driver);
static void xhci_driver_configure_runtime_registers(xhci_driver_t* driver);
static b8 xhci_driver_is_usb3_port(xhci_driver_t* driver, u8 port_num);
static u8 xhci_driver_get_port_speed(xhci_driver_t* driver, u8 port);
static xhci_command_completion_trb_t* xhci_driver_disable_slot(xhci_driver_t* driver, u8 slot_id);
static u16 xhci_driver_initial_max_packet_size(u8 speed);
static void xhci_driver_configure_ctrl_ep_input_context(xhci_driver_t* driver, xhci_device_t* device, u16 max_packet_size);
static xhci_command_completion_trb_t* xhci_driver_address_device(xhci_driver_t* driver, xhci_device_t* device, b8 bsr);
static b8 xhci_driver_send_control_transfer(xhci_driver_t* driver, xhci_device_t* device, xhci_device_request_packet_t* req, void* buffer, u16 length);
static b8 xhci_driver_get_device_descriptor(xhci_driver_t* driver, xhci_device_t* device, void* out, u16 length);
static void xhci_driver_configure_device(xhci_driver_t* driver, xhci_device_t* device, const usb_device_descriptor_t* desc);
static void xhci_driver_setup_device(xhci_driver_t* driver, u8 port);

// TODO: check allocations, xhci free memory

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

static xhci_portsc_register_t xhci_driver_read_portsc_reg(xhci_driver_t* driver, u8 port_num) {
    u64 reg_base = (u64)(driver->op_regs) + (0x400 + (0x10 * port_num));

    xhci_portsc_register_t reg;
    reg.raw = *(volatile u32*)(reg_base);

    return reg;
}

static void xhci_driver_write_portsc_reg(xhci_driver_t* driver, xhci_portsc_register_t reg, u8 port_num) {
    u64 reg_base = (u64)(driver->op_regs) + (0x400 + (0x10 * port_num));
    *(volatile u32*)(reg_base) = reg.raw;
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

static void xhci_driver_acknowledge_portsc_changes(xhci_driver_t* driver, u8 port_index, u32 change_bits) {
    xhci_portsc_register_t portsc = xhci_driver_read_portsc_reg(driver, port_index);
    portsc.raw &= ~PORTSC_RW1C_BITS; // preserve R/W bits, zero all RW1C
    portsc.raw |= (change_bits & PORTSC_RW1C_BITS); // write 1 to clear the targeted ones
    xhci_driver_write_portsc_reg(driver, portsc, port_index);
    (void)xhci_driver_read_portsc_reg(driver, port_index); // flush posted writes
}

static void xhci_driver_teardown_device(xhci_driver_t* driver, u8 port_index) {
    xhci_device_t* device = driver->port_devices[port_index];
    if (device == NULL) { return; }

    u8 slot_id = device->slot;

    // wake up any endpoint waiters so they don't hang (e.g., on disconnect)
    for (u8 i = 2; i <= XHCI_DEVICE_MAX_ENDPOINTS; ++i) {
        xhci_endpoint_t* ep = device->endpoints[i];
        if (!ep) { continue; }
        ep->completed = true;
    }

    // TODO:
    // usb_core_device_disconnected(driver, device);

    (void)xhci_driver_disable_slot(driver, slot_id); // tolerate failure (device may be gone)

    // save output_ctx before destroy() clear it
    dma_region_t output_ctx = device->output_ctx;
    xhci_device_destroy(device);
    xhci_free_memory((void*)output_ctx.virt);
    driver->dcbaa[slot_id] = 0;
    driver->port_devices[port_index] = NULL;
    driver->slot_devices[slot_id] = NULL;
    kfree(device);
}

static void xhci_driver_process_events(xhci_driver_t* driver) {
    barrier_dma_read();

    // poll the event ring for events
    u64 dequeued = 0;

    if (xhci_event_ring_has_unprocessed_events(&driver->event_ring)) {
        xhci_event_ring_dequeue_events(&driver->event_ring, _event_poll_trbs, &dequeued);
    }

    u8 command_completion_status = 0;
    for (u64 i = 0; i < dequeued; ++i) {
        xhci_trb_t* event = _event_poll_trbs[i];
        
        if (!event) { continue; }

        switch (event->trb_type) {
            case XHCI_TRB_TYPE_PORT_STATUS_CHANGE_EVENT: {
                xhci_port_status_change_trb_t* psc = (xhci_port_status_change_trb_t*)event;
                u8 port_id = psc->port_id; // 1 based

                if (port_id < 1 || port_id > driver->max_ports) {
                    printf("[XHCI DRIVER]: Port status change for invalid port %u\n", port_id);
                    break;
                }

                xhci_portsc_register_t portsc = xhci_driver_read_portsc_reg(driver, port_id - 1);

                if (portsc.csc && portsc.ccs) {
                    xhci_driver_setup_device(driver, port_id - 1);
                }
                else if (portsc.csc && !portsc.ccs) {
                    printf("[XHCI DRIVER]: Device disconnected from port %u\n", port_id);
                    xhci_driver_teardown_device(driver, port_id - 1);
                    xhci_driver_acknowledge_portsc_changes(driver, port_id - 1, PORTSC_RW1C_BITS);
                }

                break;
            }
            case XHCI_TRB_TYPE_CMD_COMPLETION_EVENT: {
                command_completion_status = 1;
                driver->command_completion_events[driver->command_completion_event_count] = (xhci_command_completion_trb_t*)event;
                ++driver->command_completion_event_count;
                break;
            }
            case XHCI_TRB_TYPE_TRANSFER_EVENT: {
                xhci_transfer_completion_trb_t* e = (xhci_transfer_completion_trb_t*)event;
                u8 slot = e->slot_id;
                u8 ep_id = e->endpoint_id;

                if (slot == 0 || slot > driver->max_device_slots || ep_id == 0 || ep_id > 31) {
                    printf("[XHCI DRIVER]: Transfer event with invalid slot=%u ep=%u\n", slot, ep_id);
                    break;
                }

                xhci_device_t* dev = driver->slot_devices[slot];
                if (!dev) { break; }

                if (ep_id == 1) {
                    // xhci_driver_complete_endpoint_transfer(driver, &dev->ctrl_result, &dev->ctrl_completed, e);
                    dev->ctrl_result = *e;
                    dev->ctrl_completed = true;
                }
                else {
                    // TODO: not sure if this is right
                    xhci_endpoint_t* ep = (xhci_endpoint_t*)dev->endpoints[ep_id];
                    if (ep == NULL) { break; }
                    // xhci_driver_complete_endpoint_transfer(driver, &dev->ctrl_result, &dev->ctrl_completed, e); 
                    ep->result = *e;
                    ep->completed = true;
                }

                break;
            }
            default: { break; } // TODO
        }
    }

    driver->command_irq_completed = command_completion_status;
}

static void xhci_driver_irq_handler(struct regs* r, void* user_data) {
    xhci_driver_t* driver = (xhci_driver_t*)user_data;
    xhci_driver_process_events(driver); // TODO: this is EXPENSIVE to do in an IRQ handler, add to queue or something, dirty way for now
    xhci_driver_acknowledge_irq(driver, 0);
}

static void xhci_driver_setup_dcbaa(xhci_driver_t* driver) {
    size_t dcbaa_size = sizeof(u64) * (driver->max_device_slots + 1);
    dma_region_t dcbaa_region = xhci_alloc_memory(dcbaa_size, XHCI_DEVICE_CONTEXT_ALIGNMENT, XHCI_DEVICE_CONTEXT_BOUNDARY);
    driver->dcbaa = (u64*)dcbaa_region.virt;
    driver->dcbaa_virtual_addresses = (u64*)kmalloc((driver->max_device_slots + 1) * sizeof(u64)); // TODO: check this

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

// TODO: probably not the best performing way, but XHCI controller doesn't support a huge number of devices
static b8 xhci_driver_is_usb3_port(xhci_driver_t* driver, u8 port_num) {
    for (u8 i = 0; i < driver->usb3_port_count; ++i) {
        if (driver->usb3_ports[i] == port_num) { return true; }
    }

    return false;
}

static u8 xhci_driver_get_port_speed(xhci_driver_t* driver, u8 port) {
    xhci_portsc_register_t portsc = xhci_driver_read_portsc_reg(driver, port);
    return (u8)portsc.port_speed;
}

static xhci_command_completion_trb_t* xhci_driver_disable_slot(xhci_driver_t* driver, u8 slot_id) {
    xhci_disable_slot_command_trb_t trb;
    memset(&trb, 0, sizeof(xhci_disable_slot_command_trb_t));
    trb.trb_type = XHCI_TRB_TYPE_DISABLE_SLOT_CMD;
    trb.slot_id = slot_id;
    return xhci_driver_send_command_trb(driver, (xhci_trb_t*)(&trb), 5000 /* just a guess.... */);
}

static u16 xhci_driver_initial_max_packet_size(u8 speed) {
    switch (speed) {
        case XHCI_USB_SPEED_LOW_SPEED:
            return 8;
        case XHCI_USB_SPEED_FULL_SPEED:
        case XHCI_USB_SPEED_HIGH_SPEED:
            return 64;
        case XHCI_USB_SPEED_SUPER_SPEED:
        case XHCI_USB_SPEED_SUPER_SPEED_PLUS:
            return 512;
        default:
            return 8;
    }

    return 8;
}

static void xhci_driver_configure_ctrl_ep_input_context(xhci_driver_t* driver, xhci_device_t* device, u16 max_packet_size) {
    size_t ctx_size = driver->sixty_four_byte_context_size ? sizeof(xhci_input_context64_t) : sizeof(xhci_input_context32_t);
    memset(xhci_device_get_input_ctrl_ctx(device), 0, ctx_size);

    xhci_input_control_context32_t* input_ctrl = xhci_device_get_input_ctrl_ctx(device);
    xhci_slot_context32_t* slot_ctx = xhci_device_get_input_slot_ctx(device);
    xhci_endpoint_context32_t* ep0_ctx = xhci_device_get_input_ctrl_ep_ctx(device);

    // enable slot context (A0) and control endpoint context (A1)
    input_ctrl->add_flags = (1 << 0) | (1 << 1);
    input_ctrl->drop_flags = 0;

    // slot context
    slot_ctx->route_string = 0;
    slot_ctx->speed = device->speed;
    slot_ctx->context_entries = 1;
    slot_ctx->root_hub_port_num = device->port + 1; // slot_ctx->root_hub_port_num is 1-based
    slot_ctx->interrupter_target = 0;

    // Control endpoint context (EP0, bidirectional)
    ep0_ctx->endpoint_state = XHCI_ENDPOINT_STATE_DISABLED;
    ep0_ctx->endpoint_type = XHCI_ENDPOINT_TYPE_CONTROL;
    ep0_ctx->max_packet_size = max_packet_size;
    ep0_ctx->max_burst_size = 0;
    ep0_ctx->error_count = 3;
    ep0_ctx->interval = 0;
    ep0_ctx->average_trb_length = 8;
    ep0_ctx->max_esit_payload_lo = 0;
    
    ep0_ctx->transfer_ring_dequeue_ptr = device->ctrl_ring->physical_base;
    ep0_ctx->dcs = device->ctrl_ring->rcs_bit;
}

static xhci_command_completion_trb_t* xhci_driver_address_device(xhci_driver_t* driver, xhci_device_t* device, b8 bsr) {
    // construct the Address Device trb
    xhci_address_device_command_trb_t address_trb;
    address_trb.input_context_physical_base = device->input_ctx.phys;
    address_trb.rsvd = 0;
    address_trb.cycle_bit = 0;
    address_trb.rsvd1 = 0;

    /*
        Block Set Address Request (BSR). When this flag is set to '0' the Address Device Command shall
        generate a USB SET_ADDRESS request to the device. When this flag is set to '1' the Address
        Device Command shall not generate a USB SET_ADDRESS request. Refer to section 4.6.5 for
        more information on the use of this flag.
    */
    address_trb.bsr = bsr ? 1 : 0;

    address_trb.trb_type = XHCI_TRB_TYPE_ADDRESS_DEVICE_CMD;
    address_trb.rsvd2 = 0;
    address_trb.slot_id = device->slot;

    return xhci_driver_send_command_trb(driver, (xhci_trb_t*)(&address_trb), 5000 /* just a guess... */);
}

static b8 xhci_driver_send_control_transfer(xhci_driver_t* driver, xhci_device_t* device, xhci_device_request_packet_t* req, void* buffer, u16 length) {
    xhci_transfer_ring_t* ring = device->ctrl_ring;

    // use the device's persistent DMA buffer
    vaddr_t dma_buffer = device->ctrl_transfer_buffer.virt;
    paddr_t dma_buffer_phys = device->ctrl_transfer_buffer.phys;

    if (((void*)dma_buffer) == NULL || ((void*)dma_buffer_phys) == NULL) {
        // TODO: throw error
        printf("[XHCI DRIVER]: Missing control transfer buffer for slot %u\n", device->slot);
        xassert(false, "");
        return false;
    }

    if (length > PAGE_SIZE) {
        // TODO: throw error
        printf("[XHCI DRIVER]: Control transfer too large (%u bytes)\n", length);
        xassert(false, "");
        return false;
    }

    b8 is_in = (req->transfer_direction != 0);

    // for OUT data stage, copy caller data into DMA buffer before enqueue
    if (length > 0 && !is_in && buffer) {
        memcpy((void*)dma_buffer, buffer, length);
    }
    else {
        memset((void*)dma_buffer, 0, length > 0 ? length : 1);
    }

    // setup stage trb
    xhci_setup_stage_trb_t setup;
    memset(&setup, 0, sizeof(xhci_setup_stage_trb_t));
    setup.trb_type = XHCI_TRB_TYPE_SETUP_STAGE;
    setup.request_packet = *req;
    setup.trb_transfer_length = 8;
    setup.interrupter_target = 0;
    setup.idt = 1;
    setup.ioc = 0;
    // TRT: 0=No Data, 2=OUT Data, 3=IN Data
    setup.trt = (length > 0) ? (is_in ? 3 : 2) : 0;

    // Data Stage TRB (if there's data to transfer)
    xhci_data_stage_trb_t data;
    memset(&data, 0, sizeof(xhci_data_stage_trb_t));
    if (length > 0) {
        data.trb_type = XHCI_TRB_TYPE_DATA_STAGE;
        data.data_buffer = dma_buffer_phys;
        data.trb_transfer_length = length;
        data.td_size = 0;
        data.interrupter_target = 0;
        data.dir = is_in ? 1 : 0;
        data.ioc = 0;
        data.idt = 0;
        data.chain = 0;
    }

    // Status Stage TRB (direction opposite to data stage)
    xhci_status_stage_trb_t status;
    memset(&status, 0, sizeof(xhci_status_stage_trb_t));
    status.trb_type = XHCI_TRB_TYPE_STATUS_STAGE;
    status.interrupter_target = 0;
    status.ioc = 1; // Interrupt on completion
    status.dir = (length > 0) ? (is_in ? 0 : 1) : 1;

    // reset EP0 completion state before doorbell
    device->ctrl_completed = false;

    // enqueue all trbs before ringing the doorbell
    // (required for QEMU compability, also safe on real hardware)
    xhci_transfer_ring_enqueue(ring, (xhci_trb_t*)(&setup));
    if (length > 0) {
        xhci_transfer_ring_enqueue(ring, (xhci_trb_t*)(&data));
    }

    xhci_transfer_ring_enqueue(ring, (xhci_trb_t*)(&status));

    xhci_doorbell_manager_ring_doorbell(&driver->doorbell_manager, device->slot, XHCI_DOORBELL_TARGET_CONTROL_EP_RING);

    // wait for transfer completion
    const u64 TRANSFER_TIMEOUT_MS = 5000;
    u64 deadline = ktimer_get_system_time_in_nanoseconds() + TRANSFER_TIMEOUT_MS * 1000000ULL;

    // TODO: look over functions
    xhci_driver_process_events(driver);
    xhci_event_ring_finish_procecssing(&driver->event_ring);

    while (!device->ctrl_completed && ktimer_get_system_time_in_nanoseconds() < deadline) {
        xhci_driver_process_events(driver);
        xhci_event_ring_finish_procecssing(&driver->event_ring);
    }

    if (!device->ctrl_completed) {
        printf("[XHCI DRIVER]: Control transfer timed out");
        return false;
    }

    if (device->ctrl_result.completion_code != XHCI_TRB_COMPLETION_CODE_SUCCESS) {
        printf("[XHCI DRIVER]: Control transfer failed: %s", xhci_trb_completion_code_to_string(device->ctrl_result.completion_code));
        return false;
    }

    // copy IN data to caller's buffer
    if (buffer && length > 0 && is_in) {
        barrier_dma_read();
        memcpy(buffer, (void*)dma_buffer, length);
    }

    return true;
}

static b8 xhci_driver_get_device_descriptor(xhci_driver_t* driver, xhci_device_t* device, void* out, u16 length) {
    xhci_device_request_packet_t req;
    req.bRequestType = 0x80; // Device to Host, Standard, Device
    req.bRequest = 6; // GET_DESCRIPTOR
    req.wValue = USB_DESCRIPTOR_REQUEST(USB_DESCRIPTOR_DEVICE, 0);
    req.wIndex = 0;
    req.wLength = length;

    return xhci_driver_send_control_transfer(driver, device, &req, out, length);
}

static void xhci_driver_configure_device(xhci_driver_t* driver, xhci_device_t* device, const usb_device_descriptor_t* desc) {
    /*
    u8 slot_id = device->slot;

    usb_configuration_descriptor_t config;
    if (!xhci_driver_get_configuration_descriptor(driver, device, &config)) {
        printf("[XHCI DRIVER]: Failed to read config descriptor for slot %u\n", slot_id);
        return;
    }
    
    printf("[XHCI DRIVER]: slot %u config: %u interface(s), totalLength=%u\n", slot_id, config.bNumInterfaces, config.wTotalLength);
    */
}

static void xhci_driver_setup_device(xhci_driver_t* driver, u8 port) {
    b8 reset_successful = xhci_driver_reset_port(driver, port);
    if (!reset_successful) {
        printf("[XHCI DRIVER]: Failed to reset port %u after connection detection\n", port);
        return;   
    }

    xhci_portsc_register_t portsc = xhci_driver_read_portsc_reg(driver, port);
    printf("[XHCI DRIVER]: Device connected on port %u - %s\n", port, usb_speed_strings[portsc.port_speed]);

    // enable a device slot
    xhci_trb_t enable_slot = XHCI_CONSTRUCT_CMD_TRB(XHCI_TRB_TYPE_ENABLE_SLOT_CMD);
    xhci_command_completion_trb_t* completion = xhci_driver_send_command_trb(driver, &enable_slot, 5000 /* just a guess... */);

    if (completion == NULL) {
        printf("[XHCI DRIVER]: Enable Slot failed for prot %u\n", port);
        return;
    }

    // allocate the output device context
    size_t dev_ctx_size = driver->sixty_four_byte_context_size ? sizeof(xhci_device_context64_t) : sizeof(xhci_device_context32_t);    
    
    dma_region_t output_ctx = xhci_alloc_memory(dev_ctx_size, XHCI_DEVICE_CONTEXT_ALIGNMENT, XHCI_DEVICE_CONTEXT_BOUNDARY); // TODO: check alignment in boundary
    if (((void*)output_ctx.virt) == NULL) {
        printf("[XHCI DRIVER]: Failed to allocate device context for slot: %u", completion->slot_id);
        xhci_driver_disable_slot(driver, completion->slot_id);
        return;
    }

    // write the physical address into DCBAA[slot_id]
    driver->dcbaa[completion->slot_id] = (u64)output_ctx.phys;

    // create and initialize the device object
    xhci_device_t* device = kmalloc(sizeof(xhci_device_t));
    if (!device) {
        printf("[XHCI DRIVER]: Failed to allocate xhci_device_t for slot %u", completion->slot_id);
        xhci_free_memory((void*)output_ctx.virt);
        driver->dcbaa[completion->slot_id] = 0;
        xhci_driver_disable_slot(driver, completion->slot_id);
        return;
    }

    *device = xhci_device_init(port, completion->slot_id, portsc.port_speed, driver->sixty_four_byte_context_size);
    device->output_ctx = output_ctx;
    driver->port_devices[port] = device;
    driver->slot_devices[completion->slot_id] = device;

    #define SETUP_FAIL(msg, ...) \
        printf(msg, ##__VA_ARGS__); \
        xhci_driver_teardown_device(driver, port); \
        xassert(false, ""); \
        return;

    // configure the input context for the Address Device command
    u16 max_packet_size = xhci_driver_initial_max_packet_size(portsc.port_speed);
    xhci_driver_configure_ctrl_ep_input_context(driver, device, max_packet_size);

    // first address device with BSR = 1, essentially blocking the SET_ADDRESS request,
    // but it still enables the control endpoint which can be used to get the device descriptor.
    // some legacy devices require their descriptor to be read before sending them a SET_ADDRESS command
    if (xhci_driver_address_device(driver, device, true) == NULL) {
        SETUP_FAIL("[XHCI DRIVER]: Address Device (BSR = 1) failed for slot %u", completion->slot_id);
    }

    // read the first 8 bytes of the device descriptor to get bMaxPacketSize0
    usb_device_descriptor_t desc;
    if (!xhci_driver_get_device_descriptor(driver, device, &desc, 8)) {
        SETUP_FAIL("[XHCI DRIVER]: Failed to read device descriptor for slot %u\n", completion->slot_id);
    }

    // if the device reported a different max packet size, update the input context
    if (desc.bMaxPacketSize0 != max_packet_size) {
        max_packet_size = desc.bMaxPacketSize0;
        xhci_driver_configure_ctrl_ep_input_context(driver, device, max_packet_size);

        // send evaluate context to update the xHCI's internal state
        xhci_evaluate_context_command_trb_t eval_ctx;
        memset(&eval_ctx, 0, sizeof(xhci_evaluate_context_command_trb_t));
        eval_ctx.trb_type = XHCI_TRB_TYPE_EVALUATE_CONTEXT_CMD;
        eval_ctx.input_context_physical_base = device->input_ctx.phys;
        eval_ctx.slot_id = completion->slot_id;

        if (xhci_driver_send_command_trb(driver, (xhci_trb_t*)(&eval_ctx), 5000 /* just a guess... */) == NULL) {
            SETUP_FAIL("[XHCI DRIVER]: Evaluate context failed for slot %u\n", completion->slot_id);
        }
    }

    // send the address device command again with BSR = 0 this time
    if (xhci_driver_address_device(driver, device, false) == NULL) {
        SETUP_FAIL("[XHCI DRIVER]: Address device (BSR = 0) failed for slot %u\n", completion->slot_id);
    }

    // sync the output device context into the input context
    xhci_device_sync_input_ctx(device);

    // read the full device descriptor
    if (!xhci_driver_get_device_descriptor(driver, device, &desc, sizeof(usb_device_descriptor_t))) {
        SETUP_FAIL("[XHCI DRIVER]: Failed to read full device descriptor for slot %u\n", completion->slot_id);
    }

    #undef SETUP_FAIL

    u8 v1 = port;
    u8 v2 = completion->slot_id;
    u32 v3 = desc.bcdUsb >> 8;
    u32 v4 = (desc.bcdUsb >> 4) & 0xF;
    u16 v5 = desc.idVendor;
    u16 v6 = desc.idProduct;
    u8 v7 = desc.bMaxPacketSize0;
    u8 v8 = desc.bNumConfigurations;

    printf("[XHCI DRIVER]: port %u slot %u: USB %x.%x vid=0x%x pid=0x%x mps0=%u configs=%u\n",
            (u32)port, (u32)completion->slot_id,
            (u32)(desc.bcdUsb >> 8), (u32)((desc.bcdUsb >> 4) & 0xF),
            (u32)desc.idVendor, (u32)desc.idProduct, (u32)desc.bMaxPacketSize0,
            (u32)desc.bNumConfigurations);

    // TODO:
    // xhci_driver_configure_device(driver, device, &desc);
}

b8 xhci_driver_init_driver(xhci_driver_t* driver) {
    pci_bar_t* bar = &driver->pci_device->bar[0];
    driver->xhc_base = xhci_map_mmio(bar->base, bar->size);

    pci_enable_bus_master(driver->pci_device); // TODO: maybe move to before and MMIO
    xhci_driver_parse_capability_registers(driver);

    xhci_driver_parse_extended_capabilities(driver);

    if (!xhci_driver_reset_host_controller(driver)) { return false; }

    xhci_driver_configure_operational_registers(driver);
    xhci_driver_configure_runtime_registers(driver);

    // allocate per-port device tracking array
    driver->port_devices = (xhci_device_t**)kmalloc((size_t)driver->max_ports * sizeof(xhci_device_t*));
    if (!driver->port_devices) {
        printf("[XHCI DRIVER]: Failed to allocate memory for driver->port_devices\n");
        return false;
    }

    driver->slot_devices = (xhci_device_t**)kmalloc(256 * sizeof(xhci_device_t*));
    if (!driver->slot_devices) {
        printf("[XHCI DRIVER]: Failed to allocate memory for driver->slot_devices\n");
        return false;
    }

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

    // controller started //

    // scan for devices connected before controller was started
    for (u8 port = 0; port < driver->max_ports; ++port) {
        xhci_portsc_register_t portsc = xhci_driver_read_portsc_reg(driver, port);
        if (portsc.csc && portsc.ccs) {
            xhci_driver_setup_device(driver, port);
        }
    }

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

void xhci_driver_parse_extended_capabilities(xhci_driver_t* driver) {
    volatile u32* head_cap_ptr = (volatile u32*)(driver->xhc_base + driver->extended_capabilities_offset);
    driver->extended_capabilities_head = xhci_extended_capability_init(head_cap_ptr);

    xhci_extended_capability_t* node = &driver->extended_capabilities_head;
    
    while (node) {
        if (node->entry.id == (u8)XHCI_EXTENDED_CAPABILITY_CODE_SUPPORTED_PROTOCOL) {
            xhci_usb_supported_protocol_capability_t cap = xhci_usb_supported_protocol_capability_init(node->base);

            // make the ports zero-based
            u8 first_port = cap.compatible_port_offset - 1;
            u8 last_port = first_port + cap.compatible_port_count - 1;

            // usb3
            if (cap.major_revision_version == 3) {
                for (u8 port = first_port; port <= last_port; ++port) {
                    xassert(driver->usb3_port_count + 1 < 255, "XHCI: Max amount of supported devices is 255!");
                    // keep track of usb3 ports
                    driver->usb3_ports[driver->usb3_port_count] = port;
                    ++driver->usb3_port_count;
                }
            } 
        }
        
        node = node->next;
    }
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

b8 xhci_driver_reset_port(xhci_driver_t* driver, u8 port_num) {
    xhci_portsc_register_t portsc = xhci_driver_read_portsc_reg(driver, port_num);

    b8 is_usb3_port = xhci_driver_is_usb3_port(driver, port_num);

    // power on the port if necessary
    if (portsc.pp == 0) {
        portsc.pp = 1;
        xhci_driver_write_portsc_reg(driver, portsc, port_num);
        msleep(20); // wait for power stabilization
        portsc = xhci_driver_read_portsc_reg(driver, port_num);

        if (portsc.pp == 0) {
            // port failed to power on
            return false;
        }
    }

    // clear any lingering status change bits before initiating the reset
    // VERY MUCH needed on real hardware
    portsc.csc = 1;
    portsc.pec = 1;
    portsc.prc = 1;
    xhci_driver_write_portsc_reg(driver, portsc, port_num);

    // initiate the port reset
    if (is_usb3_port) {
        portsc.wpr = 1; // warm reset for USB 3.0
    }
    else {
        portsc.pr = 1; // standard port reset for USB 2.0
    }
    xhci_driver_write_portsc_reg(driver, portsc, port_num);

    // wait for the reset to complete
    int timeout = 100;
    while (timeout > 0) {
        portsc = xhci_driver_read_portsc_reg(driver, port_num);

        if ((is_usb3_port && portsc.wrc) || (!is_usb3_port && portsc.prc)) {
            break; // reset complete
        }

        --timeout;
        msleep(1);
    }

    if (timeout == 0) {
        // failed to reset
        return false;
    }

    msleep(3); // give the hardware time to settle

    // clear the reset completion and status change bits
    // NOTE: VERY IMPORTANT, DO NOT REMOVE
    portsc.prc = 1;
    portsc.wrc = 1;
    portsc.csc = 1;
    portsc.pec = 1;
    portsc.ped = 0;
    xhci_driver_write_portsc_reg(driver, portsc, port_num);

    msleep(3); // give the hardware time to settle

    // re-read the register to check if the port is enabled
    portsc = xhci_driver_read_portsc_reg(driver, port_num);

    // this case could happen when the port has been reset after a device
    // disconnect event, and no device has connected since that
    if (portsc.ped == 0) {
        return false;
    }

    // successful port reset
    return true;
}

void xhci_driver_run_loop(xhci_driver_t* driver) {
    xhci_driver_process_events(driver);
    xhci_event_ring_finish_procecssing(&driver->event_ring);
}