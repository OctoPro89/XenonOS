#include <drivers/usb/xhci.h>
#include <drivers/usb/xhci_mem.h>
#include <drivers/usb/xhci_common.h>
#include <drivers/usb/xhci_device_ctx.h>
#include <drivers/usb/usb_descriptors.h>
#include <drivers/usb/core/usb_core.h>
#include <drivers/usb/core/usb_transfer.h>
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
static b8 xhci_driver_get_configuration_descriptor(xhci_driver_t* driver, xhci_device_t* device, usb_configuration_descriptor_t* desc_out, u8 config_index);
static xhci_endpoint_t* xhci_driver_create_endpoint(xhci_driver_t* driver, xhci_device_t* device, const usb_endpoint_descriptor_t* desc);
static void xhci_driver_configure_endpoint_context(xhci_driver_t* driver, xhci_device_t* device, xhci_endpoint_t* ep);
static xhci_command_completion_trb_t*  xhci_driver_configure_endpoints(xhci_driver_t* driver, xhci_device_t* device);
static b8 xhci_driver_set_configuration(xhci_driver_t* driver, xhci_device_t* device, u8 config_value);
static void xhci_driver_configure_device(xhci_driver_t* driver, xhci_device_t* device, const usb_device_descriptor_t* desc);
static void xhci_driver_setup_device(xhci_driver_t* driver, u8 port);
static b8 xhci_driver_submit_normal_transfer(xhci_driver_t* driver, xhci_device_t* device, xhci_endpoint_t* ep, void* buffer, u32 length);
static void xhci_driver_finish_request(xhci_driver_t* driver, usb_transfer_request_t* request, usb_transfer_status_t status, u32 actual_length);
static xhci_command_completion_trb_t* xhci_driver_stop_endpoint(xhci_driver_t* driver, xhci_device_t* device, u8 dci);
static b8 xhci_driver_recover_stalled_control_endpoint(xhci_driver_t* driver, xhci_device_t* device);
static xhci_command_completion_trb_t* xhci_driver_reset_endpoint(xhci_driver_t* driver, xhci_device_t* device, u8 dci);
static xhci_command_completion_trb_t* xhci_driver_set_tr_dequeue_ptr(xhci_driver_t* driver, xhci_device_t* device, u8 dci, paddr_t new_dequeue_phys, u8 dcs);
static void xhci_driver_clear_tt_buffer(xhci_driver_t* driver, xhci_device_t* device, u8 dev_addr);
static b8 xhci_driver_queue_interrupt_in_stream_td(xhci_driver_t* driver, xhci_device_t* device, xhci_endpoint_t* ep, xhci_endpoint_async_state_t* state, b8 defer_doorbell);
static void xhci_driver_queue_deferred_doorbell(xhci_driver_t* driver, u8 slot_id, u8 target);
static void xhci_driver_enqueue_pending_request(xhci_driver_t* driver, xhci_endpoint_async_state_t* state, usb_transfer_request_t* request);
static b8 xhci_driver_start_async_request(xhci_driver_t* driver, xhci_device_t* device, xhci_endpoint_t* ep, xhci_endpoint_async_state_t* state, usb_transfer_request_t* request, b8 defer_doorbell);
static void xhci_driver_complete_endpoint_transfer(xhci_driver_t* driver, xhci_transfer_completion_trb_t* result_out, b8* completed_out, const xhci_transfer_completion_trb_t* event);
static void xhci_driver_complete_interrupt_in_stream(xhci_driver_t* driver, xhci_device_t* device, xhci_endpoint_t* ep, xhci_endpoint_async_state_t* state, const xhci_transfer_completion_trb_t* event);
static b8 xhci_driver_queue_interrupt_in_stream_payload(xhci_driver_t* driver, xhci_interrupt_in_stream_state_t* stream, const u8* data, u32 length, u16 mfindex, u64 queued_t_us, u32* seq_out);
static void xhci_driver_complete_async_request(xhci_driver_t* driver, xhci_device_t* device, xhci_endpoint_t* ep, xhci_endpoint_async_state_t* state, const xhci_transfer_completion_trb_t* event);
static void xhci_driver_kick_async_request_queue(xhci_driver_t* driver, xhci_device_t* device, xhci_endpoint_t* ep, xhci_endpoint_async_state_t* state, b8 defer_doorbell);

static __hint_inline__ u32 xhci_driver_read_mfindex(const xhci_driver_t* driver) {
    if (!driver->runtime_regs) {
        return 0xFFFFu;
    }

    return driver->runtime_regs->mf_index & 0x3FFF;
}

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

    usb_core_device_disconnected(driver, device);

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

                // TODO: look at
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
                    xhci_driver_complete_endpoint_transfer(driver, &dev->ctrl_result, &dev->ctrl_completed, e);
                    dev->ctrl_result = *e;
                    dev->ctrl_completed = true;
                }
                else {
                    xhci_endpoint_t* ep = dev->endpoints[ep_id];
                    if (!ep) { break; }

                    if (ep->async_state) {
                        xhci_endpoint_async_state_t* state = ep->async_state;
                        if (state->async_enabled) {
                            if (state->interrupt_in_stream.active ||
                                state->interrupt_in_stream.closing ||
                                state->interrupt_in_stream.payloads ||
                                state->interrupt_in_stream.payload_storage) {
                                    xhci_driver_complete_interrupt_in_stream(driver, dev, ep, state, e);
                            }
                            else if (state->active_request) {
                                xhci_driver_complete_async_request(driver, dev, ep, state, e);
                            }
                        }
                        else {
                            xhci_driver_complete_endpoint_transfer(driver, &ep->result, &ep->completed, e);
                        }

                        break;
                    }

                    xhci_driver_complete_endpoint_transfer(driver, &ep->result, &ep->completed, e);

                    break;
                }
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

static b8 xhci_driver_get_configuration_descriptor(xhci_driver_t* driver, xhci_device_t* device, usb_configuration_descriptor_t* desc_out, u8 config_index) {
    xhci_device_request_packet_t req;
    memset(&req, 0, sizeof(xhci_device_request_packet_t));
    req.bRequestType = 0x80;
    req.bRequest = 6; // GET_DESCRIPTOR
    req.wValue = USB_DESCRIPTOR_REQUEST(USB_DESCRIPTOR_CONFIGURATION, config_index);
    req.wIndex = 0;

    // first pass, read the 9-byte config descriptor header to get wTotalLength
    const u16 CONFIG_HDR_SIZE = 9; // bLength + bDescriptorType + wTotalLength + 5 fields
    req.wLength = CONFIG_HDR_SIZE;
    if (!xhci_driver_send_control_transfer(driver, device, &req, desc_out, CONFIG_HDR_SIZE)) {
        xassert(false, "");
        return false;
    }

    // second pass, read the full descriptor
    u16 total_length = desc_out->wTotalLength;
    if (total_length > sizeof(usb_configuration_descriptor_t)) {
        printf("[XHCI DRIVER]: Config descriptor too large (%u bytes), clamping\n", total_length);
        total_length = sizeof(usb_configuration_descriptor_t);
        xassert(false, "");
    }

    req.wLength = total_length;
    return xhci_driver_send_control_transfer(driver, device, &req, desc_out, total_length);
}

static xhci_endpoint_t* xhci_driver_create_endpoint(xhci_driver_t* driver, xhci_device_t* device, const usb_endpoint_descriptor_t* desc) {
    xhci_endpoint_t* ep = (xhci_endpoint_t*)kmalloc(sizeof(xhci_endpoint_t));
    if (!ep) {
        printf("[XHCI DRIVER]: Failed to allocate endpoint for slot %u\n", device->slot);
        xassert(false, "");
        return NULL;
    }

    *ep = xhci_endpoint_init(device->slot, desc);

    device->endpoints[ep->dci] = ep;

    printf("[XHCI DRIVER]: EP%u %s (DCI %u), maxPacket=%u\n",
            XHCI_ENDPOINT_NUM(*ep),
            XHCI_ENDPOINT_IS_IN(*ep) ? "IN" : "OUT",
            ep->dci, ep->max_packet_size);

    return ep;
}

static void xhci_driver_configure_endpoint_context(xhci_driver_t* driver, xhci_device_t* device, xhci_endpoint_t* ep) {
    xhci_input_control_context32_t* input_ctrl = xhci_device_get_input_ctrl_ctx(device);
    xhci_slot_context32_t* slot_ctx = xhci_device_get_input_slot_ctx(device);

    // set the add flag for this endpoint's DCI
    input_ctrl->add_flags |= (1u << ep->dci);

    // update context_entries to the highest DCI
    if (ep->dci > slot_ctx->context_entries) {
        slot_ctx->context_entries = ep->dci;
    }

    // zero the endpoint context before filling to clear any stale data
    xhci_endpoint_context32_t* ep_ctx = xhci_device_get_input_ep_ctx(device, ep->dci);
    size_t ep_ctx_size = driver->sixty_four_byte_context_size ? sizeof(xhci_input_context64_t) : sizeof(xhci_input_context32_t);
    memset(ep_ctx, 0, ep_ctx_size);

    // compute xHCI interval from USB bInterval
    // for hs/ss interrupt/isoch: xHCI interval = bInterval - 1
    // for fs/ls interrupt: use raw bInterval (clamped to valid range)
    u8 xhci_interval = ep->interval;
    u8 speed = device->speed;
    if (speed == XHCI_USB_SPEED_HIGH_SPEED || speed == XHCI_USB_SPEED_SUPER_SPEED || speed == XHCI_USB_SPEED_SUPER_SPEED_PLUS) {
        if (xhci_interval > 0) {
            xhci_interval--;
        }
    }

    ep_ctx->endpoint_state = XHCI_ENDPOINT_STATE_DISABLED;
    ep_ctx->endpoint_type = ep->xhc_ep_type;
    ep_ctx->max_packet_size = ep->max_packet_size;
    ep_ctx->max_burst_size = 0;
    ep_ctx->error_count = 3;
    ep_ctx->interval = xhci_interval;
    ep_ctx->average_trb_length = ep->max_packet_size;
    ep_ctx->max_esit_payload_lo = ep->max_packet_size;
    ep_ctx->max_esit_payload_hi = 0;
    ep_ctx->transfer_ring_dequeue_ptr = ep->ring->physical_base;
    ep_ctx->dcs = ep->ring->rcs_bit;
}

static xhci_command_completion_trb_t* xhci_driver_configure_endpoints(xhci_driver_t* driver, xhci_device_t* device) {
    // ensure slot context is included in the input context
    xhci_input_control_context32_t* input_ctrl = xhci_device_get_input_ctrl_ctx(device);
    input_ctrl->add_flags |= (1u << 0);
    input_ctrl->drop_flags = 0;

    xhci_configure_endpoint_command_trb_t trb;
    memset(&trb, 0, sizeof(xhci_configure_endpoint_command_trb_t));
    trb.trb_type = XHCI_TRB_TYPE_CONFIGURE_ENDPOINT_CMD;
    trb.input_context_physical_base = device->input_ctx.phys;
    trb.slot_id = device->slot;

    return xhci_driver_send_command_trb(driver, (xhci_trb_t*)(&trb), 5000 /* just a guess... */);
}

static b8 xhci_driver_set_configuration(xhci_driver_t* driver, xhci_device_t* device, u8 config_value) {
    xhci_device_request_packet_t req;
    memset(&req, 0, sizeof(xhci_device_request_packet_t));
    req.bRequestType = 0x00; // Host to Device, Standard, Device
    req.bRequest = 9;        // SET_CONFIGURATION
    req.wValue = config_value;
    req.wIndex = 0;
    req.wLength = 0;

    return xhci_driver_send_control_transfer(driver, device, &req, NULL, 0);
}

static void xhci_driver_configure_device(xhci_driver_t* driver, xhci_device_t* device, const usb_device_descriptor_t* desc) {
    u8 slot_id = device->slot;

    usb_configuration_descriptor_t config;
    if (!xhci_driver_get_configuration_descriptor(driver, device, &config, 0)) {
        printf("[XHCI DRIVER]: Failed to read config descriptor for slot %u\n", slot_id);
        return;
    }
    
    printf("[XHCI DRIVER]: slot %u config: %u interface(s), totalLength=%u\n", slot_id, config.bNumInterfaces, config.wTotalLength);
    
    // sync the input context with the xHCI's current output context
    // so the slot and EP0 state are up to date before adding new endpoints
    xhci_device_sync_input_ctx(device);

    // reset input control context flags (clear stale bits from ADDRESS_DEVICE)
    xhci_input_control_context32_t* input_ctrl = xhci_device_get_input_ctrl_ctx(device);
    input_ctrl->add_flags = (1u << 0); // start with slot context only
    input_ctrl->drop_flags = 0;

    // parse descriptors: track interfaces and associate endpoints
    u16 offset = 0;
    u16 data_length = config.wTotalLength > 9 ? config.wTotalLength - 9 : 0;
    if (data_length > sizeof(config.data)) {
        data_length = sizeof(config.data);
    }

    xhci_interface_info_t* current_iface = NULL;

    while (offset < data_length) {
        usb_descriptor_header_t* hdr = (usb_descriptor_header_t*)(&config.data[offset]);
        if (hdr->bLength == 0) { break; }

        if (hdr->bDescriptorType == USB_DESCRIPTOR_INTERFACE) {
            if (hdr->bLength >= sizeof(usb_interface_descriptor_t) &&
                device->num_interfaces < XHCI_DEVICE_MAX_INTERFACES) {
                usb_interface_descriptor_t* iface_desc = (usb_interface_descriptor_t*)(hdr);
                u8 idx = device->num_interfaces;
                device->num_interfaces = idx + 1;
                current_iface = &device->interfaces[idx];
                current_iface->interface_number = iface_desc->bInterfaceNumber;
                current_iface->alternate_setting = iface_desc->bAlternateSetting;
                current_iface->interface_class = iface_desc->bInterfaceClass;
                current_iface->interface_subclass = iface_desc->bInterfaceSubClass;
                current_iface->interface_protocol = iface_desc->bInterfaceProtocol;
                current_iface->hid_report_desc_length = 0;
                current_iface->num_endpoints = 0;
                printf("[XHCI DRIVER]: interface %u: class=0x%x subclass=0x%x protocol=0x%x\n",
                          iface_desc->bInterfaceNumber,
                          iface_desc->bInterfaceClass,
                          iface_desc->bInterfaceSubClass,
                          iface_desc->bInterfaceProtocol);
            }
        } else if (hdr->bDescriptorType == USB_DESCRIPTOR_HID) {
            if (current_iface && hdr->bLength >= 6) {
                const u8* hid_bytes = (const u8*)(hdr);
                u8 num_desc = hid_bytes[5];
                for (u8 i = 0; i < num_desc; i++) {
                    u16 desc_offset = (u16)(6 + (i * 3));
                    if (desc_offset + 3 > hdr->bLength) {
                        break;
                    }
                    u8 desc_type = hid_bytes[desc_offset];
                    u16 desc_len = (u16)(hid_bytes[desc_offset + 1]) | ((u16)(hid_bytes[desc_offset + 2]) << 8);
                    if (desc_type == USB_DESCRIPTOR_HID_REPORT) {
                        current_iface->hid_report_desc_length = desc_len;
                        break;
                    }
                }
            }
        } else if (hdr->bDescriptorType == USB_DESCRIPTOR_ENDPOINT) {
            if (hdr->bLength >= sizeof(usb_endpoint_descriptor_t)) {
                usb_endpoint_descriptor_t* ep_desc = (usb_endpoint_descriptor_t*)(hdr);
                xhci_endpoint_t* ep = xhci_driver_create_endpoint(driver, device, ep_desc);
                if (ep) {
                    xhci_driver_configure_endpoint_context(driver, device, ep);
                    if (current_iface && current_iface->num_endpoints < 16) {
                        current_iface->endpoint_dcis[current_iface->num_endpoints++] = ep->dci;
                    }
                }
            }
        }

        offset += hdr->bLength;
    }

    if (xhci_driver_configure_endpoints(driver, device) == NULL) {
        xassert(false, "");
        return;
    }

    if (!xhci_driver_set_configuration(driver, device, config.bConfigurationValue)) {
        xassert(false, "");
        return;
    }

    printf("[XHCI DRIVER]: Slot %u configured\n", slot_id);

    // hand off to USB core for driver matching and binding
    usb_core_device_configured(driver, device, desc);
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

    xhci_driver_configure_device(driver, device, &desc);
}

static b8 xhci_driver_submit_normal_transfer(xhci_driver_t* driver, xhci_device_t* device, xhci_endpoint_t* ep, void* buffer, u32 length) {
    if (!ep || !ep->ring || !ep->dma_buffer.virt) {
        xassert(false, "");
        return false;
    }

    if (length > PAGE_SIZE) {
        printf("[XHCI DRIVER]: Normal transfer too large (%u bytes)\n", length);
        return false;
    }

    // TODO: reset completion state before doorbell to avoid race with driver event dispatch
    b8 state_rc = true;
    if (ep->async_state && ep->async_state->async_enabled) {
        state_rc = false;
    }
    ep->completed = false;

    if (!state_rc) { return false; }

    // copy OUT data into DMA buffer before enqueue
    if (XHCI_ENDPOINT_IS_IN(*ep) && buffer && length > 0) {
        memcpy((void*)ep->dma_buffer.virt, buffer, length);
    }

    if (!xhci_transfer_ring_can_enqueue(ep->ring, 1)) {
        printf("[XHCI DRIVER]: Transfer ring full for EP%u\n", XHCI_ENDPOINT_NUM(*ep));
        return false;
    }

    xhci_normal_trb_t normal;
    memset(&normal, 0, sizeof(xhci_normal_trb_t));
    normal.trb_type = XHCI_TRB_TYPE_NORMAL;
    normal.data_buffer_physical_base = ep->dma_buffer.phys;
    normal.trb_transfer_length = length;
    normal.td_size = 0;
    normal.interrupter_target = 0;
    normal.ioc = 1;
    normal.isp = XHCI_ENDPOINT_IS_IN(*ep) ? 1 : 0;
    normal.chain = 0;

    xhci_transfer_ring_enqueue(ep->ring, (xhci_trb_t*)&normal);
    xhci_doorbell_manager_ring_doorbell(&driver->doorbell_manager, device->slot, ep->dci);

    // TODO: wait for transfer completion (HCD task processes events and wakes)
    while (!ep->completed) {
        usleep(50); // TODO: no clue what to set this to
    }

    // copy IN data from DMA buffer to caller
    if (XHCI_ENDPOINT_IS_IN(*ep) && buffer && length > 0) {
        barrier_dma_read();
        memcpy(buffer, (void*)ep->dma_buffer.virt, length);
    }

    if (ep->result.completion_code != XHCI_TRB_COMPLETION_CODE_SUCCESS && ep->result.completion_code != XHCI_TRB_COMPLETION_CODE_SHORT_PACKET) {
        printf("[XHCI DRIVER]: Normal transfer failed on EP%u: %s\n", XHCI_ENDPOINT_NUM(*ep), xhci_trb_completion_code_to_string(ep->result.completion_code));
        return false;
    }

    return true;
}

static void xhci_driver_finish_request(xhci_driver_t* driver, usb_transfer_request_t* request, usb_transfer_status_t status, u32 actual_length) {
    request->status = status;
    request->actual_length = actual_length;
    request->pending = false;
    request->next = NULL;
    request->hcd_private = NULL;
    // TODO: threading
}

static xhci_command_completion_trb_t* xhci_driver_stop_endpoint(xhci_driver_t* driver, xhci_device_t* device, u8 dci) {
    xhci_stop_endpoint_command_trb_t trb;
    memset(&trb, 0, sizeof(xhci_stop_endpoint_command_trb_t));
    trb.trb_type = XHCI_TRB_TYPE_STOP_ENDPOINT_CMD;
    trb.endpoint_id = dci;
    trb.slot_id = device->slot;
    return xhci_driver_send_command_trb(driver, (xhci_trb_t*)&trb, 5000 /* just a guess...*/);
}

static b8 xhci_driver_recover_stalled_control_endpoint(xhci_driver_t* driver, xhci_device_t* device) {
    if (!driver || !device || !device->ctrl_ring) {
        xassert(false, "");
        return false;
    }

    // When a control transfer stalls, the next SETUP transaction clears the
    // USB-level stall, but xHCI still needs its EP0 state repaired. Reset the
    // endpoint and then advance the hardware dequeue pointer past the failed TD
    // so the controller does not try to replay it on the next doorbell ring.
    //
    // Match Linux behavior: always proceed with Set TR Dequeue even if Reset
    // Endpoint fails — the command will only fail if the endpoint wasn't
    // halted, and in that case we still need the dequeue pointer advanced.
    xhci_transfer_ring_t* ring = device->ctrl_ring;
    paddr_t dequeue_phys = xhci_transfer_ring_get_enqueue_phys(ring);
    u8 dequeue_cycle = ring->rcs_bit;

    // VL805 quirk: the controller cannot handle Set TR
    // Dequeue Pointer pointing at a Link TRB. If the enqueue pointer wrapped
    // and is now at index 0, the physical address is the segment start which
    // is safe; but if for any reason it points at the Link TRB slot, skip
    // past it to the segment start with the toggled cycle bit.
    paddr_t link_phys = ring->physical_base + (ring->max_trb_count - 1) * sizeof(xhci_trb_t);
    if (dequeue_phys == link_phys) {
        dequeue_phys = ring->physical_base;
        dequeue_cycle = !dequeue_cycle;
    }

    b8 rc = xhci_driver_reset_endpoint(driver, device, 1) != NULL;
    if (!rc) {
        printf("[XHCI DRIVER]: recovering stalled EP0 slot %u: reset endpoint failed (ignored)", device->slot);
    }

    if (xhci_driver_set_tr_dequeue_ptr(driver, device, 1, dequeue_phys, dequeue_cycle) != NULL) {
        printf("[XHCI DRIVER]: recovering stalled EP0 slot %u: set dequeue pointer failed\n", device->slot);
        return false;
    }

    // For FS/LS devices behind a HS hub, clear the hub's TT buffer so the
    // shared Transaction Translator does not remain stuck from the stalled
    // split transaction. Without this, a single-TT hub can block enumeration
    // and traffic for all other downstream FS/LS devices.
    if (device->output_ctx.virt) {
        u8 dev_addr = 0;
        if (driver->sixty_four_byte_context_size) {
            xhci_device_context64_t* ctx = (xhci_device_context64_t*)(device->output_ctx.virt);
            dev_addr = (u8)(ctx->slot_context.device_address);
        } else {
            xhci_device_context32_t* ctx = (xhci_device_context32_t*)(device->output_ctx.virt);
            dev_addr = (u8)(ctx->slot_context.device_address);
        }
        xhci_driver_clear_tt_buffer(driver, device, dev_addr);
    }

    return true;
}

static xhci_command_completion_trb_t* xhci_driver_reset_endpoint(xhci_driver_t* driver, xhci_device_t* device, u8 dci) {
    xhci_reset_endpoint_command_trb_t trb;
    memset(&trb, 0, sizeof(xhci_reset_endpoint_command_trb_t));
    trb.trb_type = XHCI_TRB_TYPE_RESET_ENDPOINT_CMD;
    trb.endpoint_id = dci;
    trb.slot_id = device->slot;
    return xhci_driver_send_command_trb(driver, (xhci_trb_t*)&trb, 5000 /* just a guess */);
}

static xhci_command_completion_trb_t* xhci_driver_set_tr_dequeue_ptr(xhci_driver_t* driver, xhci_device_t* device, u8 dci, paddr_t new_dequeue_phys, u8 dcs) {
    xhci_stop_endpoint_command_trb_t trb = {};
    trb.trb_type = XHCI_TRB_TYPE_STOP_ENDPOINT_CMD;
    trb.endpoint_id = dci;
    trb.slot_id = device->slot;
    return xhci_driver_send_command_trb(driver, (xhci_trb_t*)&trb, 5000 /* just a guess */);
}

static void xhci_driver_clear_tt_buffer(xhci_driver_t* driver, xhci_device_t* device, u8 dev_addr) {
    // TODO: only for USB hubs
}

static b8 xhci_driver_queue_interrupt_in_stream_td(xhci_driver_t* driver, xhci_device_t* device, xhci_endpoint_t* ep, xhci_endpoint_async_state_t* state, b8 defer_doorbell) {
    if (!device ||!ep || !state->interrupt_in_stream.active) {
        xassert(false, "");
        return false;
    }

    u32 length = state->interrupt_in_stream.payload_length;
    if (length == 0 || length > PAGE_SIZE) {
        xassert(false, "");
        return false;
    }

    b8 rc = true;
    xhci_normal_trb_t normal;
    memset(&normal, 0, sizeof(xhci_normal_trb_t));
    normal.trb_type = XHCI_TRB_TYPE_NORMAL;
    normal.data_buffer_physical_base = ep->dma_buffer.phys;
    normal.trb_transfer_length = length;
    normal.td_size = 0;
    normal.interrupter_target = 0;
    normal.ioc = 1;
    normal.isp = 1;
    normal.chain = 0;

    if (state->disconnecting || state->active_request || !ep->ring || !xhci_transfer_ring_can_enqueue(ep->ring, 1)) {
        rc = false;
    }
    else {
        xhci_transfer_ring_enqueue(ep->ring, (xhci_trb_t*)&normal);
    }

    if (!rc) {
        return false;
    }

    if (defer_doorbell) {
        xhci_driver_queue_deferred_doorbell(driver, device->slot, ep->dci);
    }
    else {
        xhci_doorbell_manager_ring_doorbell(&driver->doorbell_manager, device->slot, ep->dci);
    }

    return true;
}

static void xhci_driver_queue_deferred_doorbell(xhci_driver_t* driver, u8 slot_id, u8 target) {
    if (driver->pending_doorbell_count >= (sizeof(driver->pending_doorbells) / sizeof(driver->pending_doorbells[0]))) {
        xhci_doorbell_manager_ring_doorbell(&driver->doorbell_manager, slot_id, target);
        return;
    }

    driver->pending_doorbells[driver->pending_doorbell_count++] = (struct pending_doorbell){ .slot_id = slot_id, .target = target };
}

static void xhci_driver_enqueue_pending_request(xhci_driver_t* driver, xhci_endpoint_async_state_t* state, usb_transfer_request_t* request) {
    request->next = NULL;
    if (!state->pending_tail) {
        state->pending_head = request;
        state->pending_tail = request;
        return;
    }

    state->pending_tail->next = request;
    state->pending_tail = request;
}

static b8 xhci_driver_start_async_request(xhci_driver_t* driver, xhci_device_t* device, xhci_endpoint_t* ep, xhci_endpoint_async_state_t* state, usb_transfer_request_t* request, b8 defer_doorbell) {
    if (!device || !ep || request->requested_length > PAGE_SIZE) {
        xassert(false, "");
        return false;
    }

    b8 rc = true;
    xhci_normal_trb_t normal;
    memset(&normal, 0, sizeof(xhci_normal_trb_t));
    normal.trb_type = XHCI_TRB_TYPE_NORMAL;
    normal.data_buffer_physical_base = ep->dma_buffer.phys;
    normal.trb_transfer_length = request->requested_length;
    normal.td_size = 0;
    normal.interrupter_target = 0;
    normal.ioc = 1;
    normal.isp = XHCI_ENDPOINT_IS_IN(*ep) ? 1 : 0;
    normal.chain = 0;

    if (state->disconnecting || state->interrupt_in_stream.active || state->active_request != request || !ep->ring || !xhci_transfer_ring_can_enqueue(ep->ring, 1)) {
        rc = false;
    }
    else {
        if (!XHCI_ENDPOINT_IS_IN(*ep) && request->buffer && request->requested_length > 0) {
            memcpy((void*)ep->dma_buffer.virt, request->buffer, request->requested_length);
        }

        request->hcd_private = ep;
        xhci_transfer_ring_enqueue(ep->ring, (xhci_trb_t*)&normal);
    }

    if (!rc) {
        xassert(false, "");
        return false;
    }

    if (defer_doorbell) {
        xhci_driver_queue_deferred_doorbell(driver, device->slot, ep->dci);
    }
    else {
        xhci_doorbell_manager_ring_doorbell(&driver->doorbell_manager, device->slot, ep->dci);
    }

    return true;
}

static void xhci_driver_complete_endpoint_transfer(xhci_driver_t* driver, xhci_transfer_completion_trb_t* result_out, b8* completed_out, const xhci_transfer_completion_trb_t* event) {
    // TODO: threading
    // TODO: check
    *result_out = *event;   
    *completed_out = true;
}

static void xhci_driver_complete_interrupt_in_stream(xhci_driver_t* driver, xhci_device_t* device, xhci_endpoint_t* ep, xhci_endpoint_async_state_t* state, const xhci_transfer_completion_trb_t* event) {
    b8 was_closing = false;
    if (state->interrupt_in_stream.closing) {
        state->interrupt_in_stream.closing = false;
        was_closing = true;
    }

    if (was_closing) {
        return;
    }

    if (!state->interrupt_in_stream.active) {
        return;
    }

    if (event->completion_code == XHCI_TRB_COMPLETION_CODE_MISSED_SERVICE) {
        // The xHCI found no TD at the scheduled polling interval. This is
        // recoverable: re-arm with an immediate doorbell so the endpoint
        // resumes at the next interval without killing the stream
        if (xhci_driver_queue_interrupt_in_stream_td(driver, device, ep, state, false) != 0) {
            state->interrupt_in_stream.active = false;
        }
        return;
    }

    if (event->completion_code != XHCI_TRB_COMPLETION_CODE_SUCCESS && event->completion_code != XHCI_TRB_COMPLETION_CODE_SHORT_PACKET) {
        state->interrupt_in_stream.active = false;
        state->interrupt_in_stream.closing = false;
        return;
    }

    u32 requested = state->interrupt_in_stream.payload_length;
    u32 residual = event->transfer_length;
    u32 actual = residual <= requested ? (requested - residual) : 0;
    if (actual > requested) {
        actual = requested;
    }

    barrier_dma_read();
    if (state->interrupt_in_stream.active) {
        xhci_driver_queue_interrupt_in_stream_payload(
            driver,
            &state->interrupt_in_stream,
            (const u8*)(ep->dma_buffer.virt),
            actual,
            (u16)(xhci_driver_read_mfindex(driver)),
            ktimer_get_system_time_in_nanoseconds() / 1000,
            NULL
        );
    }

    if (!xhci_driver_queue_interrupt_in_stream_td(driver, device, ep, state, true)) {
        state->interrupt_in_stream.active = false;
    }
}

static b8 xhci_driver_queue_interrupt_in_stream_payload(xhci_driver_t* driver, xhci_interrupt_in_stream_state_t* stream, const u8* data, u32 length, u16 mfindex, u64 queued_t_us, u32* seq_out) {
    if (!stream->active || !stream->payloads || !stream->payload_storage || stream->queue_depth == 0) {
        xassert(false, "");
        return false;
    }

    if (stream->count >= stream->queue_depth) {
        stream->head = (u8)((stream->head + 1) % stream->queue_depth);
        --stream->count;
        ++stream->dropped;
    }

    u8 idx = (u8)((stream->head + stream->count) % stream->queue_depth);
    xhci_interrupt_in_payload_t* payload = &stream->payloads[idx];
    payload->seq = stream->next_seq++;
    payload->mfindex = mfindex;
    payload->len = (u16)(length);
    payload->queued_t_us = queued_t_us;
    memset(payload->data, 0, stream->payload_length);
    if (length > 0) {
        memcpy(payload->data, data, length);
    }

    ++stream->count;
    if (seq_out) {
        *seq_out = payload->seq;
    }

    return true;
}

static void xhci_driver_complete_async_request(xhci_driver_t* driver, xhci_device_t* device, xhci_endpoint_t* ep, xhci_endpoint_async_state_t* state, const xhci_transfer_completion_trb_t* event) {
    usb_transfer_request_t* request = state->active_request;
    b8 cancelled = state->active_request_cancelled;
    u32 requested = 0;

    if (request) {
        requested = request->requested_length;
    }

    state->active_request = NULL;
    state->active_request_cancelled = false;

    if (!request) {
        return;
    }

    if (cancelled) {
        xhci_driver_finish_request(driver, request, USB_TRANSFER_STATUS_CANCELLED, 0);
        xhci_driver_kick_async_request_queue(driver, device, ep, state, XHCI_ENDPOINT_TRANSFER_TYPE(*ep) == 3 && XHCI_ENDPOINT_IS_IN(*ep));
        if (!state->active_request && !state->pending_head &&
            !state->interrupt_in_stream.active &&
            !state->interrupt_in_stream.closing &&
            !state->interrupt_in_stream.payloads &&
            !state->interrupt_in_stream.payload_storage) {
            state->async_enabled = false;
        }

        return;
    }

    u32 residual = event->transfer_length;
    u32 actual = residual <= requested ? (requested - residual) : 0;
    usb_transfer_status_t status = USB_TRANSFER_STATUS_IO_ERROR;
    if (event->completion_code == XHCI_TRB_COMPLETION_CODE_SUCCESS) {
        status = USB_TRANSFER_STATUS_OK;
    }
    else if (event->completion_code == XHCI_TRB_COMPLETION_CODE_SHORT_PACKET) {
        status = (request->flags & USB_TRANSFER_FLAGS_ALLOW_SHORT) ? USB_TRANSFER_STATUS_SHORT_PACKET : USB_TRANSFER_STATUS_IO_ERROR;
        if (status == USB_TRANSFER_STATUS_IO_ERROR) {
            actual = 0;
        }
    }
    else if (event->completion_code == XHCI_TRB_COMPLETION_CODE_STALL_ERROR) {
        status = USB_TRANSFER_STATUS_STALLED;
        actual = 0;
    }
    else {
        actual = 0;
    }

    if (XHCI_ENDPOINT_IS_IN(*ep) && request->buffer && request->requested_length > 0) {
        memset(request->buffer, 0, request->requested_length);
        if ((status == USB_TRANSFER_STATUS_OK || status == USB_TRANSFER_STATUS_SHORT_PACKET && actual > 0)) {
            barrier_dma_read();
            memcpy(request->buffer, (const void*)ep->dma_buffer.virt, actual);
        }
    }

    xhci_driver_finish_request(driver, request, status, actual);
    xhci_driver_kick_async_request_queue(driver, device, ep, state, XHCI_ENDPOINT_TRANSFER_TYPE(*ep) == 3 && XHCI_ENDPOINT_IS_IN(*ep));
    if (!state->active_request && !state->pending_head &&
        !state->interrupt_in_stream.active &&
        !state->interrupt_in_stream.closing &&
        !state->interrupt_in_stream.payloads &&
        !state->interrupt_in_stream.payload_storage) {
        state->async_enabled = false;
    }
}

static void xhci_driver_kick_async_request_queue(xhci_driver_t* driver, xhci_device_t* device, xhci_endpoint_t* ep, xhci_endpoint_async_state_t* state, b8 defer_doorbell) {
    while (true) {
        usb_transfer_request_t* next = NULL;
        if (!state->disconnecting &&
            !state->interrupt_in_stream.active &&
            !state->active_request &&
            state->pending_head) {
            next = state->pending_head;
            state->pending_head = next->next;
            if (!state->pending_head) {
                state->pending_tail = NULL;
            }
            next->next = NULL;
            state->active_request = next;
            state->active_request_cancelled = false;
        }

        if (!next) {
            return;
        }

        if (xhci_driver_start_async_request(driver, device, ep, state, next, defer_doorbell)) {
            return;
        }

        if (state->active_request == next) {
            state->active_request = NULL;
            state->active_request_cancelled = false;
        }

        xhci_driver_finish_request(driver, next, USB_TRANSFER_STATUS_IO_ERROR, 0);
    }
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

    // let ports stabilize after the controller transitions to running
    // real hardware needs time for link training and device detection before port status is meaningful
    usleep(100000); // TODO: put on another thread

    // controller started //

    // scan for devices connected before controller was started
    for (u8 port = 0; port < driver->max_ports; ++port) {
        xhci_portsc_register_t portsc = xhci_driver_read_portsc_reg(driver, port);
        if (portsc.csc && portsc.ccs) {
            xhci_driver_setup_device(driver, port);
        }
    }

    // flush deferred doorbells accumulated froim scanning ports
    for (u8 i = 0; i < driver->pending_doorbell_count; ++i) {
        xhci_doorbell_manager_ring_doorbell(&driver->doorbell_manager, driver->pending_doorbells[i].slot_id, driver->pending_doorbells[i].target);
    }
    driver->pending_doorbell_count = 0;

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

b8 xhci_driver_usb_control_transfer(xhci_driver_t* driver, xhci_device_t* device, u8 request_type, u8 request, u16 value, u16 index, void* data, u16 length) {
    // TODO: scheduling here

    xhci_transfer_ring_t* ring = device->ctrl_ring;

    dma_region_t dma_buffer = device->ctrl_transfer_buffer;
    if (!dma_buffer.virt || !dma_buffer.phys) {
        printf("[XHCI DRIVER]: Missing control transfer buffer for slot %u\n", device->slot);
        return false;
    }

    if (length > PAGE_SIZE) {
        printf("[XHCI DRIVER]: Control transfer too large (%u bytes)\n", length);
        return false;
    }

    xhci_device_request_packet_t req;
    memset(&req, 0, sizeof(xhci_device_request_packet_t));
    req.bRequestType = request_type;
    req.bRequest = request;
    req.wValue = value;
    req.wIndex = index;
    req.wLength = length;

    b8 is_in = (req.transfer_direction != 0);

    if (length > 0 && !is_in && data) {
        memcpy((void*)dma_buffer.virt, data, length);
    }
    else {
        memset((void*)dma_buffer.virt, 0, length > 0 ? length : 1);
    }

    xhci_setup_stage_trb_t setup;
    memset(&setup, 0, sizeof(xhci_setup_stage_trb_t));
    setup.trb_type = XHCI_TRB_TYPE_SETUP_STAGE;
    setup.request_packet = req;
    setup.trb_transfer_length = 8;
    setup.interrupter_target = 0;
    setup.idt = 1;
    setup.ioc = 0;
    setup.trt = (length > 0) ? (is_in ? 3 : 2) : 0;

    xhci_data_stage_trb_t data_trb;
    memset(&data_trb, 0, sizeof(xhci_data_stage_trb_t));
    if (length > 0) {
        data_trb.trb_type = XHCI_TRB_TYPE_DATA_STAGE;
        data_trb.data_buffer = dma_buffer.phys;
        data_trb.trb_transfer_length = length;
        data_trb.td_size = 0;
        data_trb.interrupter_target = 0;
        data_trb.dir = is_in ? 1 : 0;
        data_trb.ioc = 0;
        data_trb.idt = 0;
        data_trb.chain = 0;
    }

    xhci_status_stage_trb_t status;
    memset(&status, 0, sizeof(xhci_status_stage_trb_t));
    status.trb_type = XHCI_TRB_TYPE_STATUS_STAGE;
    status.interrupter_target = 0;
    status.ioc = 1;
    status.dir = (length > 0) ? (is_in ? 0 : 1) : 1;

    // serialize EP0 enqueue + doorbell + wait against concurrent callers
    // TODO: mutex lock

    device->ctrl_completed = false;

    xhci_transfer_ring_enqueue(ring, (xhci_trb_t*)&setup);
    if (length > 0) {
        xhci_transfer_ring_enqueue(ring, (xhci_trb_t*)&data_trb);
    }
    xhci_transfer_ring_enqueue(ring, (xhci_trb_t*)&status);

    xhci_doorbell_manager_ring_doorbell(&driver->doorbell_manager, device->slot, XHCI_DOORBELL_TARGET_CONTROL_EP_RING);

    while (!device->ctrl_completed) {
        usleep(50); // TODO: no clue what to set this to
    }

    if (device->ctrl_result.completion_code != XHCI_TRB_COMPLETION_CODE_SUCCESS) {
        if (device->ctrl_result.completion_code == XHCI_TRB_COMPLETION_CODE_STALL_ERROR) {
            (void)xhci_driver_recover_stalled_control_endpoint(driver, device);
        }
        printf("[XHCI DRIVER]: Control transfer failed: %s\n", xhci_trb_completion_code_to_string(device->ctrl_result.completion_code));
        return false;
    }

    if (data && length > 0 && is_in) {
        barrier_dma_read();
        memcpy(data, (void*)dma_buffer.virt, length);
    }

    return true;
}

b8 xhci_driver_usb_submit_transfer(xhci_driver_t* driver, xhci_device_t* device, u8 endpoint_addr, void* buffer, u32 length) {
    if (!device || (!buffer && length > 0)) {
        return false;
    }

    xhci_endpoint_t* ep = xhci_device_endpoint_by_address(device, endpoint_addr);
    if (!ep) {
        printf("[XHCI DRIVER]: No endpoint for address %u on slot %u\n", endpoint_addr, device->slot);
        return false;
    }

    return xhci_driver_submit_normal_transfer(driver, device, ep, buffer, length);
}

void xhci_driver_usb_cancel_transfer(xhci_driver_t* driver, xhci_device_t* device, usb_transfer_request_t* request) {
    if (!device || !driver) {
        xassert(false, "");
        return;
    }

    xhci_endpoint_t* ep = xhci_device_endpoint_by_address(device, request->endpoint_addr);
    if (!ep || !ep->async_state) {
        xassert(false, "");
        return;
    }

    xhci_endpoint_async_state_t* state = ep->async_state;

    usb_transfer_request_t* prev = NULL;
    usb_transfer_request_t* crnt = NULL;
    b8 queued = false;
    b8 active = false;

    crnt = state->pending_head;
    while (crnt) {
        if (crnt == request) {
            if (prev) {
                prev->next = crnt->next;
            }
            else {
                state->pending_head = crnt->next;
            }

            if (state->pending_tail == crnt) {
                state->pending_tail = prev;
            }

            crnt->next = NULL;
            queued = true;
            break;
        }

        prev = crnt;
        crnt = crnt->next;
    }

    if (!queued && state->active_request == request) {
        state->active_request_cancelled = true;
        active = true;
    }

    if (queued) {
        xhci_driver_finish_request(driver, request, USB_TRANSFER_STATUS_CANCELLED, 0);
        if (!state->active_request && !state->pending_head &&
            !state->interrupt_in_stream.active &&
            !state->interrupt_in_stream.closing &&
            !state->interrupt_in_stream.payloads &&
            !state->interrupt_in_stream.payload_storage) {
            state->async_enabled = false;
        }
    }
    else if (active) {
        printf("[XHCI DRIVER]: Cancel request for active transfer on EP%u, will complete when hardware retires it\n", XHCI_ENDPOINT_NUM(*ep));
    }
}

b8 xhci_driver_usb_open_interrupt_in_stream(xhci_driver_t* driver, xhci_device_t* device, u8 endpoint_addr, u32 payload_length) {
    if (!device || !driver) {
        xassert(false, "");
        return false;
    }

    xhci_endpoint_t* ep = xhci_device_endpoint_by_address(device, endpoint_addr);
    if (!ep || !XHCI_ENDPOINT_IS_IN(*ep) || XHCI_ENDPOINT_TRANSFER_TYPE(*ep) != 3 || payload_length == 0 || payload_length > PAGE_SIZE) {
        xassert(false, "");
        return false;
    }

    xhci_endpoint_async_state_t* state = ep->async_state;
    if (!state) {
        xassert(false, "");
        return false;
    }

    const u8 STREAM_QUEUE_DEPTH = 2;
    xhci_interrupt_in_payload_t* payloads = (xhci_interrupt_in_payload_t*)kmalloc((size_t)(STREAM_QUEUE_DEPTH) * sizeof(xhci_interrupt_in_payload_t));
    u8* storage = (u8*)kmalloc((size_t)(STREAM_QUEUE_DEPTH) * payload_length);
    if (!payloads || !storage) {
        if (payloads) { kfree(payloads); }
        if (storage) { kfree(storage); }
        xassert(false, "");
        return false;
    }

    b8 rc = true;
    if (state->disconnecting || state->active_request || state->pending_head ||
        state->interrupt_in_stream.active || state->interrupt_in_stream.closing ||
        state->interrupt_in_stream.payloads || state->interrupt_in_stream.payload_storage) {
        rc = false;
    } else {
        state->async_enabled = true;
        state->interrupt_in_stream.active = true;
        state->interrupt_in_stream.closing = false;
        state->interrupt_in_stream.payload_length = payload_length;
        state->interrupt_in_stream.queue_depth = STREAM_QUEUE_DEPTH;
        state->interrupt_in_stream.payloads = payloads;
        state->interrupt_in_stream.payload_storage = storage;
        state->interrupt_in_stream.head = 0;
        state->interrupt_in_stream.count = 0;
        state->interrupt_in_stream.next_seq = 1;
        state->interrupt_in_stream.dropped = 0;
        for (u8 i = 0; i < STREAM_QUEUE_DEPTH; i++) {
            state->interrupt_in_stream.payloads[i].data = storage + ((size_t)(i) * payload_length);
            state->interrupt_in_stream.payloads[i].seq = 0;
            state->interrupt_in_stream.payloads[i].mfindex = 0xffff;
            state->interrupt_in_stream.payloads[i].len = 0;
            state->interrupt_in_stream.payloads[i].queued_t_us = 0;
        }
    }

    if (!rc) {
        kfree(payloads);
        kfree(storage);
        return false;
    }

    rc = xhci_driver_queue_interrupt_in_stream_td(driver, device, ep, state, false);
    if (!rc) {
        xhci_driver_usb_close_interrupt_in_stream(driver, device, endpoint_addr);
        xassert(false, "");
        return false;
    }

    return true;
}

b8 xhci_driver_usb_read_interrupt_in_stream(xhci_driver_t* driver, xhci_device_t* device, u8 endpoint_addr, void* buffer, u32 buffer_len, u32* out_length) {
    if (!device || !buffer || buffer_len == 0) {
        return false;
    }

    xhci_endpoint_t* ep = xhci_device_endpoint_by_address(device, endpoint_addr);
    if (!ep || !ep->async_state) {
        xassert(false, "");
        return false;
    }
    
    xhci_endpoint_async_state_t* state = ep->async_state;
    b8 got_payload = false;
    u32 actual = 0;
    while (true) {
        xhci_driver_run(driver); // TODO: remove
        if (!state->interrupt_in_stream.active) {
            break;
        }

        if (state->interrupt_in_stream.count > 0) {
            xhci_interrupt_in_payload_t* payload = &state->interrupt_in_stream.payloads[state->interrupt_in_stream.head];
            state->interrupt_in_stream.head = (u8)((state->interrupt_in_stream.head + 1) % state->interrupt_in_stream.queue_depth);
            state->interrupt_in_stream.count--;
            actual = payload->len < buffer_len ? payload->len : buffer_len;
            memset(buffer, 0, buffer_len);
            if (actual > 0) {
                memcpy(buffer, payload->data, actual);
            }
            got_payload = true;
            break;
        }
        usleep(50); // TODO: no clue what to set this to
    }

    if (!got_payload) {
        if (out_length) { *out_length = 0; }
        return false;
    }

    if (out_length) {
        *out_length = actual;
    }

    return true;
}

b8 xhci_driver_usb_close_interrupt_in_stream(xhci_driver_t* driver, xhci_device_t* device, u8 endpoint_addr) {
    if (!driver || !device) {
        xassert(false, "");
        return false;
    }

    xhci_endpoint_t* ep = xhci_device_endpoint_by_address(device, endpoint_addr);
    if (!ep || !ep->async_state) {
        xassert(false, "");
        return false;
    }
    
    xhci_endpoint_async_state_t* state = ep->async_state;
    u8* storage = NULL;
    xhci_interrupt_in_payload_t* payloads = NULL;
    b8 need_stop = false;
    if (state->interrupt_in_stream.active || state->interrupt_in_stream.closing ||
        state->interrupt_in_stream.payload_storage || state->interrupt_in_stream.payloads) {
        need_stop = state->interrupt_in_stream.active;
        state->interrupt_in_stream.active = false;
        state->interrupt_in_stream.closing = true;
        storage = state->interrupt_in_stream.payload_storage;
        payloads = state->interrupt_in_stream.payloads;
        state->interrupt_in_stream.payload_storage = NULL;
        state->interrupt_in_stream.payloads = NULL;
        state->interrupt_in_stream.payload_length = 0;
        state->interrupt_in_stream.queue_depth = 0;
        state->interrupt_in_stream.head = 0;
        state->interrupt_in_stream.count = 0;
        state->interrupt_in_stream.next_seq = 1;
        state->interrupt_in_stream.dropped = 0;
    }

    if (need_stop) {
        (void)xhci_driver_stop_endpoint(driver, device, ep->dci);
    }

    state->interrupt_in_stream.closing = true;
    if (!state->active_request && !state->pending_head) {
        state->async_enabled = false;
    }

    if (payloads) {
        kfree(payloads);
    }

    if (storage) {
        kfree(storage);
    }

    return true;
}

b8 xhci_driver_usb_submit_transfer_async(xhci_driver_t* driver, xhci_device_t* device, usb_transfer_request_t* request) {
    if (!device) {
        xassert(false, "");
        return false;
    }

    xhci_endpoint_t* ep = xhci_device_endpoint_by_address(device, request->endpoint_addr);
    if (!ep || XHCI_ENDPOINT_NUM(*ep) == 0 || request->requested_length > PAGE_SIZE || (!request->buffer && request->requested_length > 0)) {
        xassert(false, "");
        return false;
    }

    xhci_endpoint_async_state_t* state = ep->async_state;
    if (!state) {
        xassert(false, "");
        return false;
    }

    b8 rc = true;
    b8 start_now = false;
    if (state->disconnecting || state->interrupt_in_stream.active ||
        state->interrupt_in_stream.closing ||
        state->interrupt_in_stream.payloads ||
        state->interrupt_in_stream.payload_storage ||
        request->pending || request->next != NULL || request->hcd_private != NULL) {
        rc = -1;
    } else {
        state->async_enabled = true;
        request->actual_length = 0;
        request->status = USB_TRANSFER_STATUS_INVALID;
        request->pending = true;
        request->next = NULL;
        if (!state->active_request) {
            state->active_request = request;
            state->active_request_cancelled = false;
            start_now = true;
        } else {
            xhci_driver_enqueue_pending_request(driver, state, request);
        }
    }

    if (!rc) {
        xassert(false, "");
        return false;
    }

    if (start_now && xhci_driver_start_async_request(driver, device, ep, state, request, false)) {
        if (state->active_request == request) {
            state->active_request = NULL;
            state->active_request_cancelled = false;
        }
        request->pending = false;
        request->status = USB_TRANSFER_STATUS_IO_ERROR;

        request->hcd_private = NULL;
        return false;
    }

    return true;
}

void xhci_driver_release_disconnected_device(xhci_driver_t* driver, xhci_device_t* device) {
    if (!driver || !device) { return; }

    dma_region_t output_ctx = device->output_ctx;
    xhci_device_destroy(device);
    if (output_ctx.virt) {
        xhci_free_memory((void*)output_ctx.virt);
    }

    kfree(device);
}

void xhci_driver_run(xhci_driver_t* driver) {
    driver->pending_doorbell_count = 0;
    xhci_driver_process_events(driver);
    xhci_event_ring_finish_procecssing(&driver->event_ring);

    for (u8 i = 0; i < driver->pending_doorbell_count; ++i) {
        xhci_doorbell_manager_ring_doorbell(&driver->doorbell_manager, driver->pending_doorbells[i].slot_id, driver->pending_doorbells[i].target);
    }

    // TODO: hub crap
}