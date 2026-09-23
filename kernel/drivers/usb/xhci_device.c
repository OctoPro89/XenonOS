#include <memory/paging.h>
#include <drivers/usb/xhci_device.h>
#include <drivers/usb/xhci_device_ctx.h>
#include <xlibc/string.h>
#include <xlibc/stdlib.h>

static void xhci_device_alloc_input_ctx(xhci_device_t* dev) {
    // calculate the input context size based on the capability register parameters
    u64 input_context_size = dev->use_64byte_ctx ? sizeof(xhci_input_context64_t) : sizeof(xhci_input_context32_t);

    // allocate the input context memory
    dev->input_ctx = xhci_alloc_memory(input_context_size, XHCI_DEVICE_CONTEXT_ALIGNMENT, XHCI_DEVICE_CONTEXT_BOUNDARY); // TODO: check alignment and boundary
}

xhci_device_t xhci_device_init(u8 port, u8 slot, u8 speed, b8 use_64byte_ctx) {
    xhci_device_t dev;
    memset(&dev, 0, sizeof(xhci_device_t));
    dev.port = port;
    dev.slot = slot;
    dev.speed = speed;
    dev.use_64byte_ctx = use_64byte_ctx;
    xhci_device_alloc_input_ctx(&dev);

    // allocate a persistent DMA page for control transfer payloads
    dev.ctrl_transfer_buffer = xhci_alloc_memory(PAGE_SIZE, XHCI_TRANSFER_RING_SEGMENTS_ALIGNMENT, XHCI_TRANSFER_RING_SEGMENTS_BOUNDARY); // TODO: check alignment and boundary
    if (((void*)dev.ctrl_transfer_buffer.virt) == NULL) {
        xhci_free_memory((void*)dev.input_ctx.virt);
        dev.input_ctx.virt = (vaddr_t)NULL;
        dev.input_ctx.phys = (paddr_t)NULL;
        return dev;
    }

    // allocate and init the control transfer ring
    dev.ctrl_ring = (xhci_transfer_ring_t*)kmalloc(sizeof(xhci_transfer_ring_t));
    if (dev.ctrl_ring == NULL) {
        xhci_free_memory((void*)dev.input_ctx.virt);
        xhci_free_memory((void*)dev.ctrl_transfer_buffer.virt);
        dev.input_ctx.virt = (vaddr_t)NULL;
        dev.input_ctx.phys = (paddr_t)NULL;
        dev.ctrl_ring = NULL;
        return dev;
    }

    *dev.ctrl_ring = xhci_transfer_ring_init(XHCI_TRANSFER_RING_TRB_COUNT, dev.slot);

    return dev;
}

void xhci_device_destroy(xhci_device_t* device) {
    // TODO:
}

xhci_input_control_context32_t* xhci_device_get_input_ctrl_ctx(xhci_device_t* device) {
    if (device->use_64byte_ctx) {
        xhci_input_context64_t* input_ctx = (xhci_input_context64_t*)device->input_ctx.virt;
        return (xhci_input_control_context32_t*)(&input_ctx->control_context);
    }
    else {
        xhci_input_context32_t* input_ctx = (xhci_input_context32_t*)device->input_ctx.virt;
        return &input_ctx->control_context;
    }
}

xhci_slot_context32_t* xhci_device_get_input_slot_ctx(xhci_device_t* device) {
    if (device->use_64byte_ctx) {
        xhci_input_context64_t* input_ctx = (xhci_input_context64_t*)device->input_ctx.virt;
        return (xhci_slot_context32_t*)(&input_ctx->device_context.slot_context);
    }
    else {
        xhci_input_context32_t* input_ctx = (xhci_input_context32_t*)device->input_ctx.virt;
        return &input_ctx->device_context.slot_context;
    }
}

xhci_endpoint_context32_t* xhci_device_get_input_ctrl_ep_ctx(xhci_device_t* device) {
    if (device->use_64byte_ctx) {
        xhci_input_context64_t* input_ctx = (xhci_input_context64_t*)device->input_ctx.virt;
        return (xhci_endpoint_context32_t*)(&input_ctx->device_context.control_ep_context);
    }
    else {
        xhci_input_context32_t* input_ctx = (xhci_input_context32_t*)device->input_ctx.virt;
        return &input_ctx->device_context.control_ep_context;
    }
}

xhci_endpoint_context32_t* xhci_device_get_input_ep_ctx(xhci_device_t* device, u8 endpoint_num) {
    u8 endpoint_index = endpoint_num - 2;
    
    if (device->use_64byte_ctx) {
        xhci_input_context64_t* input_ctx = (xhci_input_context64_t*)device->input_ctx.virt;
        return (xhci_endpoint_context32_t*)(&input_ctx->device_context.ep[endpoint_index]);
    }
    else {
        xhci_input_context32_t* input_ctx = (xhci_input_context32_t*)device->input_ctx.virt;
        return &input_ctx->device_context.ep[endpoint_index];
    }
}

// Copies data from the output device context into the input context
void xhci_device_sync_input_ctx(xhci_device_t* device) {
    if (device->use_64byte_ctx) {
        xhci_input_context64_t* input_ctx = (xhci_input_context64_t*)device->input_ctx.virt;
        xhci_device_context64_t* input_device_ctx = &input_ctx->device_context;
        memcpy(input_device_ctx, (const void*)device->output_ctx.virt, sizeof(xhci_device_context64_t));
    }
    else {
        xhci_input_context32_t* input_ctx = (xhci_input_context32_t*)device->input_ctx.virt;
        xhci_device_context32_t* input_device_ctx = &input_ctx->device_context;
        memcpy(input_device_ctx, (const void*)device->output_ctx.virt, sizeof(xhci_device_context32_t));
    }
}