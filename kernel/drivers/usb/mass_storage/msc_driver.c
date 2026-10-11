#include <drivers/usb/mass_storage/msc_driver.h>

#include <xlibc/stdio.h>
#include <xlibc/stdlib.h>
#include <xlibc/string.h>
#include <xlibc/xassert.h>

#include <drivers/usb/usb_descriptors.h>

#define MSC_BOT_RESET          0xFF

#define SCSI_TEST_UNIT_READY   0x00
#define SCSI_REQUEST_SENSE     0x03
#define SCSI_INQUIRY           0x12
#define SCSI_READ_CAPACITY_10  0x25
#define SCSI_READ_10           0x28

#define USB_TRANSFER_TYPE_BULK 0x02

/**
 * @brief SCSI multi-byte response fields are big-endian.
 * BOT CBW/CSW integer fields use the host's little-endian
 * representation on x86-64 kernel
 */
static u32 msc_be32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8)  | ((u32)p[3]);
}

/**
 * @brief
 * Execute one Bulk-Only Transport command.
 *
 * cdb: SCSI command bytes
 * cdb_len: number of valid CDB bytes (1..16)
 * data: optional data-phase buffer
 * data_len: expected data-phase length
 * data_in: true for device-to-host, false for host-to-device
 *
 * Returns true only if CBW, optional data phase, and CSW all
 * report success. A false result does not yet attempt BOT reset
 * recovery or REQUEST SENSE automatically.
 */
// TODO: better error handling
static b8 msc_bot_command(msc_driver_t* self, const u8* cdb, u8 cdb_len, void* data, u32 data_len, b8 data_in) {
    if (!self || !self->dev || !cdb || cdb_len == 0 || cdb_len > 16 || (data_len != 0 && !data)) {
        xassert(false, "");
        return false;
    }

    if (self->disconnected || self->dev->disconnect_pending) {
        return false;
    }

    msc_bot_cbw_t cbw;
    memset(&cbw, 0, sizeof(cbw));

    u32 tag = ++self->bot.next_tag;
    if (tag == 0) {
        tag = ++self->bot.next_tag;
    }

    cbw.signature = MSC_BOT_CBW_SIGNATURE;
    cbw.tag = tag;
    cbw.transfer_length = data_len;
    cbw.flags = data_len && data_in ? MSC_BOT_CBW_FLAG_IN : MSC_BOT_CBW_FLAG_OUT;
    cbw.lun = self->bot.lun;
    cbw.command_length = cdb_len;
    memcpy(cbw.command, cdb, cdb_len);

    if (!usb_bulk_transfer(self->dev, self->bot.bulk_out_endpoint, &cbw, MSC_BOT_CBW_SIZE)) {
        printf("[USB-MSC]: CBW transfer failed\n");
        return false;
    }

    if (data_len != 0) {
        u8 ep = data_in ? self->bot.bulk_in_endpoint : self->bot.bulk_out_endpoint;

        if (!usb_bulk_transfer(self->dev, ep, data, data_len)) {
            printf("[USB-MSC]: Data phase failed (tag=%u)\n", tag);
            return false;
        }
    }

    msc_bot_csw_t csw;
    memset(&csw, 0, sizeof(csw));
    if (!usb_bulk_transfer(self->dev, self->bot.bulk_in_endpoint, &csw, MSC_BOT_CSW_SIZE)) {
        printf("[USB-MSC]: CSW transfer failed (tag=%u)\n", tag);
        return false;
    }

    if (csw.signature != MSC_BOT_CSW_SIGNATURE) {
        printf("[USB-MSC]: Invalid CSW signature 0x%x\n", csw.signature);
        return false;
    }

    if (csw.tag != tag) {
        printf("[USB-MSC]: CSW tag mismatch: got %u expected %u\n", csw.tag, tag);
        return false;
    }

    if (csw.status != 0) {
        printf("[USB-MSC]: SCSI command failed, status=%u, residue=%u\n", csw.status, csw.residue);
        return false;
    }

    return true;
}

static b8 msc_scsi_inquiry(msc_driver_t* self) {
    u8 cdb[6];
    memset(cdb, 0, sizeof(cdb));
    u8 response[36];
    memset(response, 0, sizeof(response));

    cdb[0] = SCSI_INQUIRY;
    cdb[4] = sizeof(response);

    if (!msc_bot_command(self, cdb, sizeof(cdb), response, sizeof(response), true)) {
        printf("[USB-MSC]: INQUIRY failed!\n");
        return false;
    }

    printf("[USB-MSC]: Vendor:");
    for (u8 i = 0; i < 8; ++i) {
        putc((char)response[8 + i]);
    }
    putc('\n');

    printf("[USB-MSC]: Product:");
    for (u8 i = 0; i < 16; ++i) {
        putc((char)response[16 + i]);
    }
    putc('\n');

    printf("[USB-MSC]: Revision:");
    for (u8 i = 0; i < 4; ++i) {
        putc((char)response[32 + i]);
    }
    putc('\n');

    return true;
}

static b8 msc_scsi_test_unit_ready(msc_driver_t* self) {
    u8 cdb[6];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_TEST_UNIT_READY;
    return msc_bot_command(self, cdb, sizeof(cdb), NULL, 0, false);
}

static b8 msc_scsi_request_sense(msc_driver_t* self) {
    u8 cdb[6];
    memset(cdb, 0, sizeof(cdb));
    u8 response[16];
    memset(response, 0, sizeof(response));

    cdb[0] = SCSI_REQUEST_SENSE;
    cdb[4] = sizeof(response);

    if (!msc_bot_command(self, cdb, sizeof(cdb), response, sizeof(response), true)) {
        printf("[USB-MSC]: REQUEST SENSE failed\n");
        return false;
    }

    printf("[USB-MSC]: Sense key=0x%x, ASC=0x%x, ASCQ=0x%x\n", response[2] & 0x0F, response[12], response[13]);

    return true;
}

static b8 msc_scsi_read_capacity(msc_driver_t* self) {
    u8 cdb[10];
    memset(cdb, 0, sizeof(cdb));
    u8 response[8];
    memset(response, 0, sizeof(response));
    
    cdb[0] = SCSI_READ_CAPACITY_10;

    if (!msc_bot_command(self, cdb, sizeof(cdb), response, sizeof(response), true)) {
        printf("[USB-MSC]: READ CAPACITY (10) failed\n");
        return false;
    }

    u32 last_lba = msc_be32(&response[0]);
    u32 block_size = msc_be32(&response[4]);

    if (block_size == 0) {
        printf("[USB-MSC]: Device reported block_size == 0\n");
        return false;
    }

    self->block_count = (u64)last_lba + 1;
    self->block_size = block_size;

    printf("[USB-MSC]: last_lba=%u, block_size=%u bytes, capacity=%llu bytes\n", last_lba, block_size, (unsigned long long)(self->block_count * self->block_size));

    return true;
}

static const usb_endpoint_t* msc_find_endpoint(const usb_interface_t* iface, u8 transfer_type, b8 direction_in) {
    if (!iface) { return NULL; }

    for (u8 i = 0; i < iface->num_endpoints; ++i) {
        const usb_endpoint_t* ep = &iface->endpoints[i];

        if (ep->transfer_type != transfer_type) {
            continue;
        }

        if (USB_ENDPOINT_IS_IN(*ep) != direction_in) {
            continue;
        }

        return ep;
    }

    return NULL;
}

static b8 msc_driver_probe(void* _self, usb_device_t* dev, usb_interface_t* iface) {
    msc_driver_t* self = (msc_driver_t*)_self;

    if (!self || !dev || !iface) {
        return false;
    }

    self->dev = dev;
    self->iface = iface;

    if (iface->interface_class != USB_CLASS_MASS_STORAGE) {
        return false;
    }

    if (iface->interface_subclass != MSC_SUBCLASS_SCSI) {
        printf("[USB-MSC]: Unsupported subclass %u\n", iface->interface_subclass);
        return false;
    }

    if (iface->interface_protocol != MSC_PROTOCOL_BULK_ONLY) {
        printf("[USB-MSC]: Unsupported protocol 0x%x\n", iface->interface_protocol);
        return false;
    }

    const usb_endpoint_t* ep_in = msc_find_endpoint(iface, USB_TRANSFER_TYPE_BULK, true);
    const usb_endpoint_t* ep_out = msc_find_endpoint(iface, USB_TRANSFER_TYPE_BULK, false);

    if (!ep_in || !ep_out) {
        printf("[USB-MSC]: Missing bulk endpoint (IN=%u, OUT=%u)\n", ep_in != NULL, ep_out != NULL);
        return false;
    }

    self->bot.bulk_in_endpoint = ep_in->address;
    self->bot.bulk_out_endpoint = ep_out->address;
    self->bot.next_tag = 0;
    self->bot.lun = 0;

    self->block_count = 0;
    self->block_size = 0;
    self->initialized = false;
    self->disconnected = false;

    printf("[USB-MSC]: Found compatible Mass Storage interface %u\n", iface->interface_number);
    printf("[USB-MSC]: Bulk IN=0x%x, OUT=0x%x\n", ep_in->address, ep_out->address);
    
    return true;
}

static void msc_driver_run(void* _self) {
    msc_driver_t* self = (msc_driver_t*)_self;

    if (!self || self->disconnected) {
        return;
    }

    printf("[USB-MSC]: Initializing BOT device...\n");

    if (!msc_scsi_inquiry(self)) {
        printf("[USB-MSC]: Initialization stopped at INQUIRY\n");
        return;
    }

    if (!msc_scsi_test_unit_ready(self)) {
        printf("[USB-MSC]: TEST UNIT READY failed; requesting sense\n");
        msc_scsi_request_sense(self);
        printf("[USB-MSC]: Media is not ready or command failed\n");
        return;
    }

    task_yield(); // nothing to do yet
}

static void msc_driver_disconnect(void* _self) {
    msc_driver_t* self = (msc_driver_t*)_self;

    if (!self) {
        return;
    }

    self->disconnected = true;
    self->initialized = false;

    printf("[USB-MSC]: Device disconnected\n");
}

static void msc_driver_destroy(void* _self) {
    msc_driver_t* self = (msc_driver_t*)_self;

    if (!self) {
        return;
    }

    self->iface = NULL;
    self->initialized = false;
}

IUSBDRIVER* msc_driver_factory(usb_device_t* dev, usb_interface_t* iface) {
    (void)dev;
    (void)iface;

    msc_driver_t* drv = (msc_driver_t*)kmalloc(sizeof(msc_driver_t));
    xassert(drv, "Failed to allocate driver!");
    if (!drv) { return NULL; }
    memset(drv, 0, sizeof(msc_driver_t));
    drv->name = "USB-MSC";
    drv->finalize_create = NULL;
    drv->destroy = msc_driver_destroy;
    drv->probe = msc_driver_probe;
    drv->run = msc_driver_run;
    drv->disconnect = msc_driver_disconnect;

    return (IUSBDRIVER*)drv;
}